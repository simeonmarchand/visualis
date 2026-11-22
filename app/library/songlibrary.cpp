#include "library/songlibrary.h"

#include "import/propresenter.h"
#include "library/library.h"
#include "live/livestate.h"
#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songjson.h"
#include "song/songstore.h"

#include <QDir>
#include <QThread>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QStandardPaths>

using namespace visualis;

Q_LOGGING_CATEGORY(lcLibrary, "visualis.library")

SongLibrary::SongLibrary(QObject *parent)
    : QObject(parent)
{
}

SongLibrary::~SongLibrary()
{
    stopImport();
}

QString SongLibrary::defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("Visualis");
}

bool SongLibrary::openLibrary(const QString &folder)
{
    stopImport();
    m_store.reset();
    m_library.reset();
    m_selected.reset();
    Library::OpenResult result = Library::open(folder);
    if (result.library) {
        m_library = std::move(result.library);
        m_store = std::make_unique<SongStore>(*m_library);
        m_error.clear();
        qCInfo(lcLibrary).noquote() << "library:" << folder;
    } else {
        m_error = result.message;
        qCWarning(lcLibrary).noquote() << "library not opened:" << folder << "-" << result.message;
    }
    emit libraryChanged();
    emit songsChanged();
    emit selectionChanged();
    return m_library != nullptr;
}

QString SongLibrary::folder() const
{
    return m_library ? m_library->folder() : QString();
}

bool SongLibrary::matches(const QString &title, const QString &search)
{
    const QStringList words = search.split(' ', Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(),
                       [&](const QString &w) { return title.contains(w, Qt::CaseInsensitive); });
}

void SongLibrary::setSearch(const QString &search)
{
    if (search == m_search)
        return;
    m_search = search;
    emit songsChanged();
}

QVariantList SongLibrary::songs() const
{
    QVariantList list;
    if (!m_store)
        return list;
    for (const SongSummary &s : m_store->list()) {
        if (matches(s.title, m_search))
            list.append(QVariantMap{{"id", s.id.toString(QUuid::WithoutBraces)}, {"title", s.title}});
    }
    return list;
}

void SongLibrary::select(const QString &id)
{
    m_selected = m_store ? m_store->load(QUuid::fromString(id)) : std::nullopt;
    emit selectionChanged();
}

QString SongLibrary::selectedId() const
{
    return m_selected ? m_selected->id.toString(QUuid::WithoutBraces) : QString();
}

QString SongLibrary::selectedTitle() const
{
    return m_selected ? m_selected->title : QString();
}

QVariantList SongLibrary::selectedSlides() const
{
    QVariantList list;
    if (!m_selected)
        return list;
    // The same expansion LiveState uses, so grid index n is live slide n.
    for (const SongSlide &s : songSlides(*m_selected)) {
        list.append(QVariantMap{{"text", s.text}, {"groupLabel", s.groupLabel}, {"groupColor", s.groupColor}});
    }
    return list;
}

bool SongLibrary::goLive(int index)
{
    // The LiveState singleton of the engine this library belongs to.
    QQmlEngine *engine = qmlEngine(this);
    if (!m_selected || !engine)
        return false;
    auto *live = engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
    return live && live->show(*m_selected, {}, index);
}

bool SongLibrary::importProPresenter(const QUrl &folder)
{
    const QString path = folder.toLocalFile();
    return startImport([path](SongStore &store, const ImportProgress &progress) {
        const propresenter::ImportReport r = propresenter::importFolder(path, store, progress);
        return ImportSummary{QStringLiteral("from ProPresenter"), r.imported, r.skipped, r.failed, r.warnings};
    });
}

bool SongLibrary::importSongs(const QUrl &folder)
{
    const QString path = folder.toLocalFile();
    return startImport([path](SongStore &store, const ImportProgress &progress) {
        const SongImportReport r = visualis::importSongs(path, store, progress);
        return ImportSummary{{}, r.imported, r.skipped, r.failed, r.warnings};
    });
}

bool SongLibrary::startImport(const ImportJob &job)
{
    if (!m_library || m_importThread)
        return false;
    m_stopImport = false;
    m_importDone = m_importTotal = 0;
    const int run = ++m_importRun;
    const QString libraryFolder = m_library->folder();
    // The worker opens the library again: a SQLite connection belongs to the thread that
    // made it. WAL and the busy timeout let both write.
    m_importThread = QThread::create([this, run, libraryFolder, job] {
        const ImportProgress progress = [this, run](int done, int total) {
            QMetaObject::invokeMethod(this, [this, run, done, total] {
                if (run != m_importRun)
                    return;
                m_importDone = done;
                m_importTotal = total;
                emit importProgressChanged();
            }, Qt::QueuedConnection);
            return !m_stopImport;
        };
        ImportSummary summary;
        Library::OpenResult opened = Library::open(libraryFolder);
        if (opened.library) {
            SongStore store(*opened.library);
            summary = job(store, progress);
        } else {
            summary.failed << std::pair{QStringLiteral("the library"), opened.message};
        }
        QMetaObject::invokeMethod(this, [this, run, summary] {
            if (run == m_importRun)
                finishImport(summary);
        }, Qt::QueuedConnection);
    });
    m_importThread->start();
    emit importingChanged();
    emit importProgressChanged();
    return true;
}

void SongLibrary::finishImport(const ImportSummary &summary)
{
    m_importThread->wait();   // it's returning: this was its last act
    delete m_importThread;
    m_importThread = nullptr;
    setReport(importReport(summary));
    emit importingChanged();
    emit importProgressChanged();
    emit songsChanged();
    emit importFinished();
}

void SongLibrary::stopImport()
{
    if (!m_importThread)
        return;
    m_stopImport = true;
    m_importThread->wait();
    delete m_importThread;
    m_importThread = nullptr;
    ++m_importRun;   // its queued progress and result are dropped
    qCInfo(lcLibrary) << "import stopped";
    emit importingChanged();
    emit importProgressChanged();
}

QString SongLibrary::importProgress() const
{
    if (!m_importThread)
        return {};
    if (m_importTotal == 0)
        return QStringLiteral("Importing…");
    return QStringLiteral("Importing %1 of %2 files…").arg(qMin(m_importDone + 1, m_importTotal)).arg(m_importTotal);
}

QString SongLibrary::importReport(const ImportSummary &s)
{
    QStringList lines{s.source.isEmpty() ? QStringLiteral("Imported %1 songs.").arg(s.imported.size())
                                         : QStringLiteral("Imported %1 songs %2.").arg(s.imported.size()).arg(s.source)};
    // Skips by reason, in the order first seen: "Skipped 3: not a song (…)."
    QStringList reasons;
    QHash<QString, int> counts;
    for (const auto &[file, reason] : s.skipped) {
        if (!counts.contains(reason))
            reasons << reason;
        ++counts[reason];
    }
    for (const QString &reason : std::as_const(reasons))
        lines << QStringLiteral("Skipped %1: %2.").arg(counts[reason]).arg(reason);
    for (const auto &[file, error] : s.failed)
        lines << QStringLiteral("Couldn't read %1: %2").arg(file, error);
    return lines.join('\n');
}

bool SongLibrary::createFromPaste(const QString &title, const QString &lyrics)
{
    if (!m_store)
        return false;
    if (title.trimmed().isEmpty() || lyrics.trimmed().isEmpty()) {
        setReport(QStringLiteral("A new song needs a title and lyrics."));
        return false;
    }
    QStringList warnings;
    Song song = songFromPastedLyrics(title.trimmed(), lyrics, {}, &warnings);
    if (const SongStore::Result saved = m_store->save(song); !saved.ok) {
        setReport(QStringLiteral("Couldn't save \"%1\": %2").arg(song.title, saved.error));
        return false;
    }
    QStringList lines{QStringLiteral("Created \"%1\".").arg(song.title)};
    lines += warnings;
    setReport(lines.join('\n'));
    emit songsChanged();
    m_selected = song;
    emit selectionChanged();
    return true;
}

bool SongLibrary::removeSong(const QString &id)
{
    if (!m_store || !m_store->remove(QUuid::fromString(id)))
        return false;
    if (m_selected && m_selected->id == QUuid::fromString(id)) {
        m_selected.reset();
        emit selectionChanged();
    }
    emit songsChanged();
    return true;
}

std::optional<Song> SongLibrary::loadSong(const QUuid &id) const
{
    return m_store ? m_store->load(id) : std::nullopt;
}

bool SongLibrary::saveSong(Song &song, QString *error)
{
    if (!m_store) {
        if (error)
            *error = QStringLiteral("The song library isn't open.");
        return false;
    }
    const SongStore::Result r = m_store->save(song);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return false;
    }
    emit songsChanged();   // the title may have changed
    if (m_selected && m_selected->id == song.id) {
        m_selected = song;
        emit selectionChanged();
    }
    return true;
}

void SongLibrary::setReport(const QString &report)
{
    m_report = report;
    qCInfo(lcLibrary).noquote() << report;
    emit reportChanged();
}
