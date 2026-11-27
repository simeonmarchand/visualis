#include "library/songlibrary.h"

#include "import/propresenter.h"
#include "library/library.h"
#include "library/templatelibrary.h"
#include "live/livestate.h"
#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songjson.h"
#include "song/songstore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QStandardPaths>

using namespace visualis;

Q_LOGGING_CATEGORY(lcLibrary, "visualis.library")

namespace {
// Files read but not yet saved; the worker waits when this many are queued.
constexpr int kMaxQueued = 200;
} // namespace

// What the worker shares with the GUI thread: files it has read, waiting to be saved here.
// The worker never touches the library or SongLibrary.
struct ImportState {
    std::atomic_bool stop = false;
    std::atomic_int total = -1;   // until the folder is listed
    QMutex mutex;
    QWaitCondition roomFree;
    QList<ImportFile> ready;
};

struct SongLibrary::ImportRun {
    QThread *thread = nullptr;
    std::shared_ptr<ImportState> state;
    QString source;
    std::unique_ptr<ImportWriter> writer;   // null once stopped
    int handled = 0;                        // files saved or reported
    bool readAll = false;                   // the worker has finished
    std::optional<ImportSummary> stopped;   // the report as it stood when stopped
};

SongLibrary::SongLibrary(QObject *parent)
    : QObject(parent)
{
    m_importSave.setInterval(10);
    connect(&m_importSave, &QTimer::timeout, this, &SongLibrary::saveImported);
    m_templates = new TemplateLibrary(this);
    connect(m_templates, &TemplateLibrary::templatesChanged, this, [this] {
        emit templatesChanged();
        emit selectionChanged();   // the grid's previews
        refreshLive();
    });
}

const TemplateSet &SongLibrary::templates() const
{
    return m_templates->templates();
}

// Only for a template change: an edit to the song itself waits for the next go-live (#44).
void SongLibrary::refreshLive()
{
    QQmlEngine *engine = qmlEngine(this);
    auto *live = engine ? engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState") : nullptr;
    if (!live || live->item().source || live->song().id.isNull())
        return;   // nothing from the library is live
    live->update(liveSong(live->song(), live->arrangementId(), templates()));   // the song as it went live
}

SongLibrary::~SongLibrary()
{
    if (!m_import)
        return;
    // Quitting mid-import: give the file in hand a moment, then leave the worker to the
    // process exit. It holds nothing of this object's.
    stopImport();
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
    // An import into the old library stops without waiting for the worker; its report says
    // what got in.
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
    if (!m_selected)
        return {};
    // What goLive() shows, so grid index n is live slide n and the tile matches the output.
    return slideMaps(liveSong(*m_selected, {}, templates()).slides);
}

bool SongLibrary::goLive(int index)
{
    // The LiveState singleton of the engine this library belongs to.
    QQmlEngine *engine = qmlEngine(this);
    if (!m_selected || !engine)
        return false;
    auto *live = engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
    return live && live->show(liveSong(*m_selected, {}, templates()), index);
}

bool SongLibrary::importProPresenter(const QUrl &folder)
{
    const QString path = folder.toLocalFile();
    return startImport({QStringLiteral("from ProPresenter"), [path] { return propresenter::presentationFiles(path); },
                        propresenter::readForImport, ImportWriter::Duplicates::SkipSameTitle});
}

bool SongLibrary::importSongs(const QUrl &folder)
{
    const QString path = folder.toLocalFile();
    return startImport({{}, [path] { return songFiles(path); }, readSongFile,
                        ImportWriter::Duplicates::SkipSameIdWarnSameTitle});
}

bool SongLibrary::startImport(const ImportJob &job)
{
    if (!m_store || m_import)
        return false;
    auto state = std::make_shared<ImportState>();
    QThread *thread = QThread::create([state, job] {
        const QFileInfoList files = job.list();
        state->total = int(files.size());
        for (const QFileInfo &file : files) {
            if (state->stop)
                return;
            ImportFile read = job.read(file);
            QMutexLocker lock(&state->mutex);
            while (state->ready.size() >= kMaxQueued && !state->stop)
                state->roomFree.wait(&state->mutex);
            if (state->stop)
                return;
            state->ready.append(std::move(read));
        }
    });
    m_import = std::make_unique<ImportRun>();
    m_import->thread = thread;
    m_import->state = state;
    m_import->source = job.source;
    m_import->writer = std::make_unique<ImportWriter>(*m_store, job.duplicates);
    connect(thread, &QThread::finished, this, [this] {
        if (!m_import)
            return;
        m_import->readAll = true;
        saveImported();   // the rest, or finish
    });
    thread->start();
    m_importSave.start();
    emit importingChanged();
    emit importProgressChanged();
    return true;
}

void SongLibrary::saveImported()
{
    if (!m_import)
        return;
    ImportRun &run = *m_import;
    ImportState &state = *run.state;
    if (run.writer) {
        bool anyReady;
        {
            QMutexLocker lock(&state.mutex);
            anyReady = !state.ready.isEmpty();
        }
        std::optional<SongStore::Batch> batch;
        if (anyReady) {
            batch.emplace(*m_store);
            run.writer->startBatch();
        }
        // The budget starts after startBatch() and always admits one file, so a big library
        // (or a slow machine) slows an import down but can't stop it.
        QElapsedTimer budget;
        budget.start();
        int saved = 0;
        while (anyReady && (saved == 0 || budget.elapsed() < kSaveBudgetMs)) {
            std::optional<ImportFile> next;
            {
                QMutexLocker lock(&state.mutex);
                if (!state.ready.isEmpty()) {
                    next = state.ready.takeFirst();
                    state.roomFree.wakeOne();
                }
            }
            if (!next)
                break;
            run.writer->add(std::move(*next));
            ++run.handled;
            ++saved;
        }
        if (saved > 0) {
            if (const SongStore::Result committed = batch->commit(); !committed.ok)
                run.writer->batchFailed(committed.error);
            emit importProgressChanged();
        }
    }
    bool drained;
    {
        QMutexLocker lock(&state.mutex);
        drained = state.ready.isEmpty();
    }
    if (run.readAll && (drained || !run.writer))
        finishImport();
}

void SongLibrary::stopImport()
{
    if (!m_import || !m_import->writer)
        return;
    m_importSave.stop();
    m_import->stopped = importSummary();
    m_import->stopped->stopped = m_import->handled < m_import->state->total || !m_import->readAll;
    m_import->writer.reset();   // it saves into the library that's closing
    m_import->state->stop = true;
    {
        QMutexLocker lock(&m_import->state->mutex);
        m_import->state->ready.clear();
        m_import->state->roomFree.wakeAll();
    }
    emit importProgressChanged();
    if (m_import->readAll)   // finish once the caller is done switching libraries
        QMetaObject::invokeMethod(this, &SongLibrary::finishImport, Qt::QueuedConnection);
    else
        connect(m_import->thread, &QThread::finished, this, &SongLibrary::finishImport, Qt::SingleShotConnection);
}

SongLibrary::ImportSummary SongLibrary::importSummary() const
{
    const SongImportReport &r = m_import->writer->report();
    return ImportSummary{m_import->source, r.imported, r.skipped, r.failed, r.warnings, false,
                         m_import->handled, m_import->state->total};
}

void SongLibrary::finishImport()
{
    if (!m_import)
        return;
    m_importSave.stop();
    m_import->thread->wait();   // already finished: returns at once
    delete m_import->thread;
    const ImportSummary summary = m_import->stopped ? *m_import->stopped : importSummary();
    m_import.reset();
    setReport(importReport(summary));
    if (summary.stopped)
        qCInfo(lcLibrary) << "import stopped after" << summary.filesDone << "of" << summary.filesTotal << "files";
    emit importingChanged();
    emit importProgressChanged();
    emit songsChanged();
    emit importFinished();
}

QString SongLibrary::importProgress() const
{
    if (!m_import)
        return {};
    if (!m_import->writer)
        return QStringLiteral("Stopping the import…");
    const int total = m_import->state->total;
    if (total <= 0)
        return QStringLiteral("Importing…");
    return QStringLiteral("Importing %1 of %2 files…").arg(qMin(m_import->handled + 1, total)).arg(total);
}

QString SongLibrary::importReport(const ImportSummary &s)
{
    QStringList lines;
    if (s.stopped && s.filesTotal < 0)
        lines << QStringLiteral("The import was stopped before its files were counted.");
    else if (s.stopped)
        lines << QStringLiteral("The import was stopped after %1 of %2 files.").arg(s.filesDone).arg(s.filesTotal);
    lines << (s.source.isEmpty() ? QStringLiteral("Imported %1 %2.").arg(s.imported.size()).arg(s.noun)
                                 : QStringLiteral("Imported %1 %2 %3.").arg(s.imported.size()).arg(s.noun, s.source));
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

SongStore::Result SongLibrary::saveSong(Song &song, Library::Transaction::Lock lock)
{
    QList<Song> songs{song};
    const SongStore::Result r = saveSongs(songs, lock);
    if (r.ok)
        song = songs.first();
    return r;
}

SongStore::Result SongLibrary::saveSongs(QList<Song> &songs, Library::Transaction::Lock lock)
{
    if (!m_store)
        return {false, QStringLiteral("The song library isn't open.")};
    const SongStore::Result r = m_store->saveAll(songs, lock);
    if (!r.ok)
        return r;
    emit songsChanged();   // a title may have changed
    for (const Song &song : songs) {
        if (m_selected && m_selected->id == song.id) {
            m_selected = song;
            emit selectionChanged();
        }
    }
    return r;   // what's live stays as shown until the next go-live (#44)
}

SongStore::Result SongLibrary::saveSlideOverrides(Song &song, Library::Transaction::Lock lock)
{
    if (!m_store)
        return {false, QStringLiteral("The song library isn't open.")};
    const SongStore::Result r = m_store->saveSlideOverrides(song, lock);
    if (r.ok && m_selected && m_selected->id == song.id) {
        m_selected = song;
        emit selectionChanged();
    }
    return r;
}

void SongLibrary::setReport(const QString &report)
{
    m_report = report;
    qCInfo(lcLibrary).noquote() << report;
    emit reportChanged();
}
