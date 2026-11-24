#include "library/medialibrary.h"
#include "library/songlibrary.h"
#include "library/library.h"
#include "media/mediafiles.h"
#include "media/medialimits.h"
#include "media/mediaprobe.h"
#include "media/mediastore.h"

#include <QDeadlineTimer>
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
using namespace std::chrono_literals;

// One file the worker has finished with. Its copy and thumbnail are still in .partial/: only
// the GUI thread publishes them, and only it deletes published files.
struct MediaImportFile {
    QString name;
    std::optional<MediaAsset> asset;   // probed: everything but id and file
    QString extension;
    QString partial;                   // the copy
    QString thumbnailPartial;          // empty if none
    QString skipped;
    QString error;
    QStringList warnings;
    QDeadlineTimer lockDeadline = QDeadlineTimer(QDeadlineTimer::Forever);   // set once the library is busy
    bool waitedForLock = false;
    bool createdFile = false;   // publishing put the file there, rather than finding it there
};

namespace {

// A partial copy older than this was left by a crash; anything younger may be another
// Visualis copying into the same library (#97 review).
constexpr auto kStalePartial = 1h;

void removePartials(const MediaImportFile &f)
{
    if (!f.partial.isEmpty())
        QFile::remove(f.partial);
    if (!f.thumbnailPartial.isEmpty())
        QFile::remove(f.thumbnailPartial);
}

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

// On the worker: copy, probe, and thumbnail, all inside .partial/.
MediaImportFile readOne(const QString &path, const QString &mediaFolder, const std::atomic_bool &stop, bool *cancelled)
{
    MediaImportFile out;
    out.name = QFileInfo(path).fileName();
    const std::optional<MediaAsset::Kind> kind = mediaKindFor(out.name);
    if (!kind) {
        out.skipped = QStringLiteral("not an image or video Visualis can show");
        return out;
    }
    const PartialCopy copy = copyToPartial(path, mediaFolder, sizeLimit(*kind), &stop);
    if (copy.cancelled) {
        *cancelled = true;
        return out;
    }
    if (!copy.ok()) {
        out.error = copy.error;
        return out;
    }
    MediaProbe probe = probeMedia(copy.path, *kind);
    if (!probe.ok()) {
        QFile::remove(copy.path);
        out.skipped = probe.error;
        return out;
    }
    out.partial = copy.path;
    out.extension = copy.extension;
    if (!probe.thumbnail.isNull()) {
        out.thumbnailPartial = newPartialPath(mediaFolder, QStringLiteral(".jpg"));
        if (!probe.thumbnail.save(out.thumbnailPartial, "JPG", 85)) {
            QFile::remove(out.thumbnailPartial);
            out.thumbnailPartial.clear();
            probe.warnings << QStringLiteral("no thumbnail could be saved");
        }
    }
    out.warnings = probe.warnings;
    out.asset = MediaAsset{{}, copy.hash, {}, out.name, *kind, probe.width, probe.height,
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
    QList<MediaImportFile> finished;   // guarded by mutex
};

struct MediaLibrary::ImportRun {
    QThread *thread = nullptr;
    bool threadDone = false;
    std::shared_ptr<ImportState> state;
    QString mediaFolder;
    bool discard = false;          // the library it was copying into has been closed
    QList<MediaImportFile> pending;   // finished by the worker, not yet saved, in order
    bool waitingForLock = false;
    SongLibrary::ImportSummary summary;
};

MediaLibrary::MediaLibrary(SongLibrary *songs, QObject *parent)
    : QObject(parent)
    , m_songs(songs)
{
    m_importPoll.setInterval(100);
    connect(&m_importPoll, &QTimer::timeout, this, &MediaLibrary::saveFinishedFiles);
    if (m_songs)
        connect(m_songs, &SongLibrary::libraryChanged, this, &MediaLibrary::libraryChanged);
    libraryChanged();
}

MediaLibrary::~MediaLibrary()
{
    if (!m_import)
        return;
    // As SongLibrary: stop, give the file in hand a moment, and leave the worker to the
    // process exit if it's still busy. It holds nothing of this object's. Unsaved copies stay
    // in .partial/ and are swept once stale.
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
        // Stop without waiting. Nothing it finishes is published: its copies are all still
        // in .partial/ and are removed as they arrive (#97 review).
        m_import->state->stop = true;
        m_import->discard = true;
        emit importProgressChanged();
    }
    m_store.reset();
    Library *library = m_songs ? m_songs->library() : nullptr;
    if (library) {
        removeStalePartialCopies(library->mediaPath(), kStalePartial);
        m_store = std::make_unique<MediaStore>(*library);
    }
    m_assets = m_store ? m_store->list() : QList<MediaAsset>{};
    emit assetsChanged();
    if (m_import)
        saveFinishedFiles();
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
            MediaImportFile f = readOne(path, mediaFolder, state->stop, &cancelled);
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
    connect(thread, &QThread::finished, this, [this] {
        m_import->threadDone = true;
        saveFinishedFiles();
    });
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

void MediaLibrary::setLockWaitLimit(int ms)
{
    m_lockWaitLimitMs = ms;
}

bool MediaLibrary::saveOne(MediaImportFile &f)
{
    SongLibrary::ImportSummary &summary = m_import->summary;
    if (m_import->discard || !m_store) {
        removePartials(f);
        return true;
    }
    if (!f.skipped.isEmpty()) {
        summary.skipped << std::pair{f.name, f.skipped};
        return true;
    }
    if (!f.error.isEmpty() || !f.asset) {
        summary.failed << std::pair{f.name, f.error};
        return true;
    }
    // The same content is already an asset, under whatever extension: one file, one row.
    if (const std::optional<MediaAsset> existing = m_store->findByHash(f.asset->contentHash)) {
        const QFileInfo there(m_store->filePath(*existing));
        if (there.isFile() && there.size() == QFileInfo(f.partial).size()) {
            removePartials(f);
            summary.skipped << std::pair{f.name, QStringLiteral("already in the library as \"%1\"").arg(existing->originalName)};
            return true;
        }
        // A crash or power loss left the row without its whole file (#113). This copy has the
        // same content, so it takes the file's place under the existing row.
        const Publish published = publishPartial(f.partial, m_import->mediaFolder, existing->file);
        f.partial.clear();
        if (!published.ok()) {
            removePartials(f);
            summary.failed << std::pair{f.name, published.error};
            return true;
        }
        if (!f.thumbnailPartial.isEmpty()) {
            publishPartial(f.thumbnailPartial, m_import->mediaFolder, thumbnailFile(existing->contentHash));
            f.thumbnailPartial.clear();
        }
        summary.imported << f.name;
        summary.warnings << QStringLiteral("%1: replaced the library's missing or damaged copy of \"%2\"")
                                .arg(f.name, existing->originalName);
        return true;
    }

    MediaAsset &asset = *f.asset;
    if (asset.file.isEmpty()) {
        asset.file = mediaFileFor(asset.contentHash, f.extension);
        const Publish published = publishPartial(f.partial, m_import->mediaFolder, asset.file);
        f.partial.clear();
        f.createdFile = published.ok() && !published.existed;
        if (!published.ok()) {
            removePartials(f);
            summary.failed << std::pair{f.name, published.error};
            return true;
        }
        if (!f.thumbnailPartial.isEmpty()) {
            if (const Publish thumb = publishPartial(f.thumbnailPartial, m_import->mediaFolder, thumbnailFile(asset.contentHash));
                !thumb.ok())
                f.warnings << QStringLiteral("no thumbnail: %1").arg(thumb.error);
            f.thumbnailPartial.clear();
        }
    }
    // The file must be there when its row is saved: a row with no file shows nothing on Sunday.
    const QString path = containedPath(m_import->mediaFolder, asset.file);
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        summary.failed << std::pair{f.name, QStringLiteral("its copy in the library went missing")};
        return true;
    }

    const MediaStore::Result r = m_store->save(asset);
    if (r.busy) {
        if (!f.waitedForLock) {
            f.waitedForLock = true;
            f.lockDeadline = QDeadlineTimer(m_lockWaitLimitMs);
        }
        if (!f.lockDeadline.hasExpired())
            return false;   // try again on the next poll; never wait here
    }
    if (!r.ok) {
        // Another Visualis saved the same content first (the hash is unique): a skip.
        if (const std::optional<MediaAsset> existing = m_store->findByHash(asset.contentHash)) {
            summary.skipped << std::pair{f.name, QStringLiteral("already in the library as \"%1\"").arg(existing->originalName)};
            return true;
        }
        // Unlink only a file this import put there and no row uses. One that was already
        // there may be another import's, about to be saved.
        if (f.createdFile && !m_store->isReferenced(asset.file)) {
            QFile::remove(path);
            if (const QString thumb = containedPath(m_import->mediaFolder, thumbnailFile(asset.contentHash)); !thumb.isEmpty())
                QFile::remove(thumb);
        }
        summary.failed << std::pair{f.name, r.busy ? QStringLiteral("another program kept the library busy; import it again")
                                                   : r.error};
        return true;
    }
    summary.imported << f.name;
    for (const QString &w : std::as_const(f.warnings))
        summary.warnings << QStringLiteral("%1: %2").arg(f.name, w);
    return true;
}

void MediaLibrary::saveFinishedFiles()
{
    if (!m_import)
        return;
    {
        QMutexLocker lock(&m_import->state->mutex);
        m_import->pending << std::exchange(m_import->state->finished, {});
    }
    const qsizetype before = m_import->summary.imported.size();
    bool waiting = false;
    while (!m_import->pending.isEmpty()) {
        if (!saveOne(m_import->pending.first())) {
            waiting = true;   // in order: the rest wait behind it
            break;
        }
        m_import->pending.removeFirst();
    }
    if (m_import->summary.imported.size() != before) {
        m_assets = m_store->list();
        emit assetsChanged();
    }
    m_import->waitingForLock = waiting;
    emit importProgressChanged();
    if (m_import->threadDone && m_import->pending.isEmpty())
        finishImport();
}

void MediaLibrary::finishImport()
{
    m_importPoll.stop();
    std::unique_ptr<ImportRun> run = std::move(m_import);
    run->thread->wait();   // already finished: returns at once
    delete run->thread;
    if (!run->discard) {
        SongLibrary::ImportSummary &summary = run->summary;
        summary.filesDone = run->state->done;
        summary.filesTotal = run->state->total;   // -1 if stopped before the files were counted
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
    if (!m_store)
        return false;
    // On the GUI thread, like every import's publish and save, so a delete can't interleave
    // with them: a pending import of the same content publishes its own copy afterwards.
    if (const MediaStore::Result r = m_store->remove(QUuid::fromString(id)); !r.ok) {
        setReport(r.busy ? QStringLiteral("Couldn't delete it: another program is using the library. Try again.") : r.error);
        return false;
    }
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
            {"thumbnail", !thumb.isEmpty() && QFile::exists(thumb) ? QUrl::fromLocalFile(thumb) : QUrl()},
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
    if (m_import->waitingForLock)
        return QStringLiteral("Waiting for the library: another program is using it…");
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
