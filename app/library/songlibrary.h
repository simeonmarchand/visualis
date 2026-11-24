#pragma once

#include "song/song.h"
#include "song/songimport.h"
#include "song/songstore.h"
#include "template/template.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>

class QThread;
class TemplateLibrary;

namespace visualis {
class Library;
class LiveState;
class SongStore;
}

// The operator window's view of the song library: the searchable song list, the selected
// song's slides, taking a slide live, and getting songs in (ProPresenter files, Visualis
// JSON, pasted lyrics). Every write goes straight to the library; there's no Save.
class SongLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString folder READ folder NOTIFY libraryChanged)
    // Why the library couldn't be opened; empty when it's open.
    Q_PROPERTY(QString error READ error NOTIFY libraryChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY songsChanged)
    // {id, title} for songs matching the search, by title.
    Q_PROPERTY(QVariantList songs READ songs NOTIFY songsChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    // {text, groupLabel, groupColor} per slide of the selected song, in play order.
    Q_PROPERTY(QVariantList selectedSlides READ selectedSlides NOTIFY selectionChanged)
    // What the last import or paste did, for the operator.
    Q_PROPERTY(QString report READ report NOTIFY reportChanged)
    // The report cut to a few lines for the library pane; the full one is behind Details.
    Q_PROPERTY(QString reportSummary READ reportSummary NOTIFY reportChanged)
    // An import is running or stopping (#69); "Importing 120 of 800 files…" says how far
    // it's got.
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QString importProgress READ importProgress NOTIFY importProgressChanged)

public:
    explicit SongLibrary(QObject *parent = nullptr);
    ~SongLibrary() override;

    // Where the library lives unless --library says otherwise: ~/Documents/Visualis.
    static QString defaultFolder();

    // Opens (or creates) the library. On failure the list is empty and `error` says why;
    // the rest of the app keeps working.
    Q_INVOKABLE bool openLibrary(const QString &folder);

    Q_INVOKABLE void select(const QString &id);
    // Takes slide `index` of the selected song live.
    Q_INVOKABLE bool goLive(int index);

    // Imports read and parse files on a worker thread (#69) and save what they read here, in
    // batches of a few milliseconds between events (#77): this library's connection is its
    // only writer, so no save ever waits on another connection's lock, and the operator
    // window and outputs keep going. One at a time: false, with nothing started,
    // while another runs or stops. When it's done the song list refreshes, `report` says what
    // it did, and importFinished() is emitted. Opening another library stops an import after
    // the file in hand, without waiting for it; its report says what it got through.
    Q_INVOKABLE bool importProPresenter(const QUrl &folder);
    Q_INVOKABLE bool importSongs(const QUrl &folder);   // a folder of Visualis JSON exports
    // Creates a song from pasted lyrics and selects it. Returns false (with `report` saying
    // why) when there's no title or no lyrics.
    Q_INVOKABLE bool createFromPaste(const QString &title, const QString &lyrics);
    // Deletes a song. What's live keeps showing (LiveState holds its own copy).
    Q_INVOKABLE bool removeSong(const QString &id);

    // For the song editor: load a song, and save it whole (every edit saves at once).
    std::optional<visualis::Song> loadSong(const QUuid &id) const;
    bool saveSong(visualis::Song &song, QString *error);

    // The open library, or null; for ServiceList, which keeps services in it.
    visualis::Library *library() const { return m_library.get(); }
    // The library's templates (#93), which this owns, and their snapshot.
    TemplateLibrary *templateLibrary() const { return m_templates; }
    const visualis::TemplateSet &templates() const;

    QString folder() const;
    QString error() const { return m_error; }
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList songs() const;
    QString selectedId() const;
    QString selectedTitle() const;
    QVariantList selectedSlides() const;
    QString report() const { return m_report; }
    QString reportSummary() const { return summarizeReport(m_report, kReportSummaryLines); }
    bool importing() const { return m_import != nullptr; }
    QString importProgress() const;

    // Whether a title matches the search: every word of it appears, ignoring case.
    static bool matches(const QString &title, const QString &search);

signals:
    void libraryChanged();
    void songsChanged();
    void selectionChanged();
    void templatesChanged();
    void reportChanged();
    void importingChanged();
    void importProgressChanged();
    void importFinished();

public:
    // What an import did. Both importers' results become one of these, so the report is
    // written in one place (importReport).
    struct ImportSummary {
        QString source;   // "from ProPresenter", or empty
        QStringList imported;
        QList<std::pair<QString, QString>> skipped;   // file name, reason
        QList<std::pair<QString, QString>> failed;    // file name, error
        QStringList warnings;
        bool stopped = false;   // stopped part way: the lists say what was done before it
        int filesDone = 0;
        int filesTotal = 0;   // -1 when stopped before the files were counted
        QString noun = QStringLiteral("songs");   // what was imported: "songs", or "files" for media
    };
    static QString importReport(const ImportSummary &summary);
    // The first `maxLines - 1` lines and an "…and N more" line, or the report if it fits.
    static QString summarizeReport(const QString &report, int maxLines);
    static constexpr int kReportSummaryLines = 6;

    // Time spent saving imported songs per batch, before the batch commits (one disk sync).
    static constexpr int kSaveBudgetMs = 5;

private:
    // How an import lists and reads its files (on the worker), and what counts as a song
    // already in the library (when saving).
    struct ImportJob {
        QString source;
        std::function<QFileInfoList()> list;
        std::function<visualis::ImportFile(const QFileInfo &)> read;
        visualis::ImportWriter::Duplicates duplicates;
    };
    struct ImportRun;
    bool startImport(const ImportJob &job);
    void saveImported();   // one time-budgeted batch of what the worker has read
    void stopImport();     // tells the worker to stop and keeps the report so far
    void finishImport();
    ImportSummary importSummary() const;
    void setReport(const QString &report);
    // Re-renders a song taken live from the library after its template changed.
    void refreshLive();

    std::unique_ptr<visualis::Library> m_library;
    std::unique_ptr<visualis::SongStore> m_store;
    QString m_error;
    QString m_search;
    std::optional<visualis::Song> m_selected;
    QString m_report;
    // The running import; null when none. The worker only touches its shared state, never
    // this object, so it can outlive it at quit.
    std::unique_ptr<ImportRun> m_import;
    QTimer m_importSave;   // saves what the worker has read, a batch at a time
    TemplateLibrary *m_templates = nullptr;   // a child; follows m_library
};
