#pragma once

#include "song/song.h"
#include "song/songstore.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

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
    // An import is running (#69); "Importing 120 of 800 files…" says how far it's got.
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

    // Imports run on a worker thread with their own library connection (#69), so the
    // operator window and outputs keep going. One at a time: false, with nothing started,
    // while another runs. When it's done the song list refreshes, `report` says what it did,
    // and importFinished() is emitted.
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

    QString folder() const;
    QString error() const { return m_error; }
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList songs() const;
    QString selectedId() const;
    QString selectedTitle() const;
    QVariantList selectedSlides() const;
    QString report() const { return m_report; }
    bool importing() const { return m_importThread != nullptr; }
    QString importProgress() const;

    // Whether a title matches the search: every word of it appears, ignoring case.
    static bool matches(const QString &title, const QString &search);

signals:
    void libraryChanged();
    void songsChanged();
    void selectionChanged();
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
    };
    static QString importReport(const ImportSummary &summary);

private:
    using ImportJob = std::function<ImportSummary(visualis::SongStore &, const visualis::ImportProgress &)>;
    bool startImport(const ImportJob &job);
    void finishImport(const ImportSummary &summary);
    // Stops a running import (the file in hand finishes) and waits for it; its result is dropped.
    void stopImport();
    void setReport(const QString &report);

    std::unique_ptr<visualis::Library> m_library;
    std::unique_ptr<visualis::SongStore> m_store;
    QString m_error;
    QString m_search;
    std::optional<visualis::Song> m_selected;
    QString m_report;
    QThread *m_importThread = nullptr;
    std::atomic_bool m_stopImport = false;
    int m_importRun = 0;   // tells a stopped import's late signals from the current one's
    int m_importDone = 0;
    int m_importTotal = 0;
};
