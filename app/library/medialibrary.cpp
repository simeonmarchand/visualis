#include "library/medialibrary.h"
#include "library/songlibrary.h"
#include "library/library.h"
#include "media/mediafiles.h"
#include "media/medialimits.h"
#include "media/mediaprobe.h"
#include "media/mediastore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QQmlEngine>
#include <QThread>

#include <atomic>

Q_LOGGING_CATEGORY(lcMedia, "visualis.media")

using namespace visualis;

namespace {

// One file the worker has finished with, waiting for the GUI thread to save it.
struct FinishedFile {
    QString name;
    std::optional<MediaAsset> asset;   // copied and probed: everything but the id
    bool existed = false;              // its content was already in media/
    QString skipped;
    QString error;
    QStringList warnings;
};

qint64 sizeLimit(MediaAsset::Kind kind)
{
    return kind == MediaAsset::Kind::Video ? medialimits::kVideoBytes : medialimits::kImageBytes;
}

// The worker's files: those given, and the images and videos directly inside given folders.
QStringList expand(const QStringList &paths)
{
    QStringList out;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isDir()) {
            out << path;
            continue;
        }
        for (const QFileInfo &f : QDir(path).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            if (mediaKindFor(f.fileName()))
                out << f.absoluteFilePath();
        }
    }
    return out;
}

FinishedFile importOne(const QString &path, const QString &mediaFolder, const std::atomic_bool &stop, bool *cancelled)
{
    FinishedFile out;
    out.name = QFileInfo(path).fileName();
    const std::optional<MediaAsset::Kind> kind = mediaKindFor(out.name);
    if (!kind) {
        out.skipped = QStringLiteral("not an image or video Visualis can show");
        return out;
    }
    const MediaCopy copy = copyIntoMedia(path, mediaFolder, sizeLimit(*kind), &stop);
    if (copy.cancelled) {
        *cancelled = true;
        return out;
    }
    if (!copy.ok()) {
        out.error = copy.error;
        return out;
    }
    const QDir media(mediaFolder);
    MediaProbe probe = probeMedia(media.filePath(copy.file), *kind);
    if (!probe.ok()) {
        if (!copy.existed)   // a copy another asset may be using is left alone
            QFile::remove(media.filePath(copy.file));
        out.skipped = probe.error;
        return out;
    }
    const QString thumb = media.filePath(thumbnailFile(copy.hash));
    if (!probe.thumbnail.isNull() && !QFile::exists(thumb)) {
        if (!media.mkpath(QFileInfo(thumb).path()) || !probe.thumbnail.save(thumb, "JPG", 85))
            probe.warnings << QStringLiteral("no thumbnail could be saved");
    }
    out.existed = copy.existed;
    out.warnings = probe.warnings;
    out.asset = MediaAsset{{}, copy.hash, copy.file, out.name, *kind, probe.width, probe.height,
                           probe.durationMs, probe.frameRate, probe.codec, {}};
    return out;
}

QString duration(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString details(const MediaAsset &a)
{
    QStringList parts;
    if (a.width > 0 && a.height > 0)
        parts << QStringLiteral("%1 × %2").arg(a.width).arg(a.height);
    if (a.kind == MediaAsset::Kind::Video) {
        if (a.durationMs > 0)
            parts << duration(a.durationMs);
        if (a.frameRate > 0)
            parts << QStringLiteral("%1 fps").arg(QString::number(a.frameRate, 'g', 4));
        if (!a.codec.isEmpty())
            parts << a.codec;
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

struct MediaLibrary::ImportState {
    std::atomic_bool stop = false;
    std::atomic_int done = 0;
    std::atomic_int total = -1;   // until the folders are listed
    QMutex mutex;
    QList<FinishedFile> finished;   // guarded by mutex
};

struct MediaLibrary::ImportRun {
    QThread *thread = nullptr;
    std::shared_ptr<ImportState> state;
    QString mediaFolder;
    bool discard = false;   // the library it was copying into has been closed
    SongLibrary::ImportSummary summary;
};

MediaLibrary::MediaLibrary(SongLibrary *songs, QObject *parent)
    : QObject(parent)
    , m_songs(songs)
{
    m_importPoll.setInterval(100);
    connect(&m_importPoll, &QTimer::timeout, this, [this] {
        saveFinishedFiles();
        emit importProgressChanged();
    });
    if (m_songs)
        connect(m_songs, &SongLibrary::libraryChanged, this, &MediaLibrary::libraryChanged);
    libraryChanged();
}

MediaLibrary::~MediaLibrary()
{
    if (!m_import)
        return;
    // As SongLibrary: stop, give the file in hand a moment, and leave the worker to the
    // process exit if it's still busy. It holds nothing of this object's.
    m_import->state->stop = true;
    disconnect(m_import->thread, nullptr, this, nullptr);
    if (m_import->thread->wait(3000))
        delete m_import->thread;
    else
        qCWarning(lcMedia) << "media import still running at quit";
}

MediaLibrary *MediaLibrary::create(QQmlEngine *qml, QJSEngine *)
{
    return new MediaLibrary(qml->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary"));
}

void MediaLibrary::libraryChanged()
{
    if (m_import) {
        // Stop without waiting; what it finishes for the old library is dropped (#97).
        m_import->state->stop = true;
        m_import->discard = true;
        emit importProgressChanged();
    }
    m_store.reset();
    Library *library = m_songs ? m_songs->library() : nullptr;
    if (library) {
        if (!m_import || m_import->mediaFolder != library->mediaPath())
            removePartialCopies(library->mediaPath());
        m_store = std::make_unique<MediaStore>(*library);
    }
    m_assets = m_store ? m_store->list() : QList<MediaAsset>{};
    emit assetsChanged();
}

bool MediaLibrary::importFiles(const QList<QUrl> &urls)
{
    if (!m_store || !m_songs || !m_songs->library() || m_import)
        return false;
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    if (paths.isEmpty())
        return false;

    auto state = std::make_shared<ImportState>();
    const QString mediaFolder = m_songs->library()->mediaPath();
    QThread *thread = QThread::create([state, paths, mediaFolder] {
        const QStringList files = expand(paths);
        state->total = int(files.size());
        for (const QString &path : files) {
            if (state->stop)
                break;
            bool cancelled = false;
            FinishedFile f = importOne(path, mediaFolder, state->stop, &cancelled);
            if (cancelled)
                break;
            {
                QMutexLocker lock(&state->mutex);
                state->finished << std::move(f);
            }
            ++state->done;
        }
    });
    m_import = std::make_unique<ImportRun>();
    m_import->thread = thread;
    m_import->state = state;
    m_import->mediaFolder = mediaFolder;
    m_import->summary.noun = QStringLiteral("files");
    connect(thread, &QThread::finished, this, &MediaLibrary::finishImport);
    thread->start();
    m_importPoll.start();
    emit importingChanged();
    emit importProgressChanged();
    return true;
}

void MediaLibrary::stopImport()
{
    if (!m_import)
        return;
    m_import->state->stop = true;
    emit importProgressChanged();
}

void MediaLibrary::saveFinishedFiles()
{
    if (!m_import)
        return;
    QList<FinishedFile> files;
    {
        QMutexLocker lock(&m_import->state->mutex);
        files.swap(m_import->state->finished);
    }
    SongLibrary::ImportSummary &summary = m_import->summary;
    const QDir media(m_import->mediaFolder);
    bool saved = false;
    for (FinishedFile &f : files) {
        if (m_import->discard || !m_store) {
            // Nothing in the closed library will ever point at a new copy.
            if (f.asset && !f.existed) {
                QFile::remove(media.filePath(f.asset->file));
                QFile::remove(media.filePath(thumbnailFile(f.asset->contentHash)));
            }
            continue;
        }
        if (!f.skipped.isEmpty()) {
            summary.skipped << std::pair{f.name, f.skipped};
            continue;
        }
        if (!f.error.isEmpty() || !f.asset) {
            summary.failed << std::pair{f.name, f.error};
            continue;
        }
        if (const std::optional<MediaAsset> existing = m_store->findByHash(f.asset->contentHash)) {
            summary.skipped << std::pair{f.name, QStringLiteral("already in the library as \"%1\"").arg(existing->originalName)};
            continue;
        }
        if (const MediaStore::Result r = m_store->save(*f.asset); !r.ok) {
            if (!f.existed)
                QFile::remove(media.filePath(f.asset->file));
            summary.failed << std::pair{f.name, r.error};
            continue;
        }
        saved = true;
        summary.imported << f.name;
        for (const QString &w : std::as_const(f.warnings))
            summary.warnings << QStringLiteral("%1: %2").arg(f.name, w);
    }
    if (saved) {
        m_assets = m_store->list();
        emit assetsChanged();
    }
}

void MediaLibrary::finishImport()
{
    m_importPoll.stop();
    m_import->thread->wait();   // already finished: returns at once
    saveFinishedFiles();
    std::unique_ptr<ImportRun> run = std::move(m_import);
    delete run->thread;
    if (!run->discard) {
        SongLibrary::ImportSummary &summary = run->summary;
        summary.filesDone = run->state->done;
        summary.filesTotal = qMax(int(run->state->total), summary.filesDone);
        summary.stopped = run->state->stop && (summary.filesTotal < 0 || summary.filesDone < summary.filesTotal);
        setReport(SongLibrary::importReport(summary));
    }
    emit importingChanged();
    emit importProgressChanged();
    emit importFinished();
}

QStringList MediaLibrary::usedBy(const QString &id) const
{
    return m_store ? m_store->usedBy(QUuid::fromString(id)) : QStringList{};
}

bool MediaLibrary::removeAsset(const QString &id)
{
    if (!m_store || !m_store->remove(QUuid::fromString(id)))
        return false;
    m_assets = m_store->list();
    emit assetsChanged();
    return true;
}

void MediaLibrary::setSearch(const QString &search)
{
    if (search == m_search)
        return;
    m_search = search;
    emit assetsChanged();
}

QVariantList MediaLibrary::assets() const
{
    QVariantList out;
    for (const MediaAsset &a : m_assets) {
        if (!SongLibrary::matches(a.originalName, m_search))
            continue;
        const QString thumb = m_store ? m_store->thumbnailPath(a) : QString();
        out << QVariantMap{
            {"id", a.id.toString(QUuid::WithoutBraces)},
            {"name", a.originalName},
            {"kind", kindName(a.kind)},
            {"thumbnail", QFile::exists(thumb) ? QUrl::fromLocalFile(thumb) : QUrl()},
            {"details", details(a)},
        };
    }
    return out;
}

QString MediaLibrary::reportSummary() const
{
    return SongLibrary::summarizeReport(m_report, SongLibrary::kReportSummaryLines);
}

QString MediaLibrary::importProgress() const
{
    if (!m_import)
        return {};
    if (m_import->state->stop)
        return QStringLiteral("Stopping the import…");
    const int total = m_import->state->total;
    if (total <= 0)
        return QStringLiteral("Importing…");
    return QStringLiteral("Importing %1 of %2 files…").arg(qMin(m_import->state->done + 1, total)).arg(total);
}

void MediaLibrary::setReport(const QString &report)
{
    m_report = report;
    qCInfo(lcMedia).noquote() << report;
    emit reportChanged();
}
