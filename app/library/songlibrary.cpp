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

// What the worker shares with the GUI thread. The worker writes `summary` before it ends;
// the GUI reads it only after QThread::finished.
struct ImportState {
    std::atomic_bool stop = false;
    std::atomic_int done = 0;
    std::atomic_int total = 0;
    SongLibrary::ImportSummary summary;
};

struct SongLibrary::ImportRun {
    QThread *thread = nullptr;
    std::shared_ptr<ImportState> state;
    // The library an import was stopped to switch away from: closed once the worker is done,
    // so closing it never waits on the worker's connection.
    std::unique_ptr<SongStore> retiredStore;
    std::unique_ptr<Library> retiredLibrary;
};

SongLibrary::SongLibrary(QObject *parent)
    : QObject(parent)
{
    m_importPoll.setInterval(100);
    connect(&m_importPoll, &QTimer::timeout, this, &SongLibrary::importProgressChanged);
}

SongLibrary::~SongLibrary()
{
    if (!m_import)
        return;
    // Quitting mid-import: give the file in hand a moment, then leave the worker to the
    // process exit. It holds nothing of this object's.
    m_import->state->stop = true;
    disconnect(m_import->thread, nullptr, this, nullptr);
    if (m_import->thread->wait(3000))
        delete m_import->thread;
    else
        qCWarning(lcLibrary) << "import still running at quit; left to finish its file";
}

QString SongLibrary::defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("Visualis");
}

bool SongLibrary::openLibrary(const QString &folder)
{
    if (m_import) {
        // Stop it without waiting; the old library closes when the worker is done.
        m_import->state->stop = true;
        if (!m_import->retiredLibrary) {
            m_import->retiredStore = std::move(m_store);
            m_import->retiredLibrary = std::move(m_library);
        }
        emit importProgressChanged();
    }
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
    if (!m_library || m_import)
        return false;
    auto state = std::make_shared<ImportState>();
    const QString libraryFolder = m_library->folder();
    // The worker opens the library again: a SQLite connection belongs to the thread that
    // made it. WAL and the busy timeout let both write. It closes its library before it
    // ends, so the GUI never waits on that.
    QThread *thread = QThread::create([state, libraryFolder, job] {
        const ImportProgress progress = [state](int done, int total) {
            state->done = done;
            state->total = total;
            return !state->stop;
        };
        ImportSummary summary;
        {
            Library::OpenResult opened = Library::open(libraryFolder);
            if (opened.library) {
                SongStore store(*opened.library);
                summary = job(store, progress);
            } else {
                summary.failed << std::pair{QStringLiteral("the library"), opened.message};
            }
        }
        summary.filesDone = state->done;
        summary.filesTotal = state->total;
        summary.stopped = state->stop && state->done < state->total;
        state->summary = summary;
    });
    m_import = std::make_unique<ImportRun>();
    m_import->thread = thread;
    m_import->state = state;
    connect(thread, &QThread::finished, this, &SongLibrary::finishImport);
    thread->start();
    m_importPoll.start();
    emit importingChanged();
    emit importProgressChanged();
    return true;
}

void SongLibrary::finishImport()
{
    m_importPoll.stop();
    std::unique_ptr<ImportRun> run = std::move(m_import);
    run->thread->wait();   // already finished: returns at once
    delete run->thread;
    const ImportSummary &summary = run->state->summary;
    setReport(importReport(summary));
    if (summary.stopped)
        qCInfo(lcLibrary) << "import stopped after" << summary.filesDone << "of" << summary.filesTotal << "files";
    run.reset();   // closes a library this import outlived
    emit importingChanged();
    emit importProgressChanged();
    emit songsChanged();
    emit importFinished();
}

QString SongLibrary::importProgress() const
{
    if (!m_import)
        return {};
    if (m_import->state->stop)
        return QStringLiteral("Stopping the import…");
    const int total = m_import->state->total;
    if (total == 0)
        return QStringLiteral("Importing…");
    return QStringLiteral("Importing %1 of %2 files…").arg(qMin(m_import->state->done + 1, total)).arg(total);
}

QString SongLibrary::importReport(const ImportSummary &s)
{
    QStringList lines;
    if (s.stopped)
        lines << QStringLiteral("The import was stopped after %1 of %2 files.").arg(s.filesDone).arg(s.filesTotal);
    lines << (s.source.isEmpty() ? QStringLiteral("Imported %1 songs.").arg(s.imported.size())
                                 : QStringLiteral("Imported %1 songs %2.").arg(s.imported.size()).arg(s.source));
    // Skips grouped by reason, in the order first seen, each file named so it can be found.
    QStringList reasons;
    QHash<QString, QStringList> files;
    for (const auto &[file, reason] : s.skipped) {
        if (!files.contains(reason))
            reasons << reason;
        files[reason] << file;
    }
    for (const QString &reason : std::as_const(reasons)) {
        lines << QStringLiteral("Skipped %1: %2:").arg(files[reason].size()).arg(reason);
        for (const QString &file : std::as_const(files[reason]))
            lines << QStringLiteral("  %1").arg(file);
    }
    for (const auto &[file, error] : s.failed)
        lines << QStringLiteral("Couldn't read %1: %2").arg(file, error);
    lines += s.warnings;
    return lines.join('\n');
}

QString SongLibrary::summarizeReport(const QString &report, int maxLines)
{
    QStringList lines = report.split('\n');
    if (lines.size() <= maxLines)
        return report;
    const qsizetype hidden = lines.size() - (maxLines - 1);
    lines.resize(maxLines - 1);
    lines << QStringLiteral("…and %1 more (see Details)").arg(hidden);
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
