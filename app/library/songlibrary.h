#pragma once

#include "song/song.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

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

    Q_INVOKABLE void importProPresenter(const QUrl &folder);
    Q_INVOKABLE void importSongs(const QUrl &folder);   // a folder of Visualis JSON exports
    // Creates a song from pasted lyrics and selects it. Returns false (with `report` saying
    // why) when there's no title or no lyrics.
    Q_INVOKABLE bool createFromPaste(const QString &title, const QString &lyrics);

    QString folder() const;
    QString error() const { return m_error; }
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList songs() const;
    QString selectedId() const;
    QString selectedTitle() const;
    QVariantList selectedSlides() const;
    QString report() const { return m_report; }

    // Whether a title matches the search: every word of it appears, ignoring case.
    static bool matches(const QString &title, const QString &search);

signals:
    void libraryChanged();
    void songsChanged();
    void selectionChanged();
    void reportChanged();

private:
    void setReport(const QString &report);

    std::unique_ptr<visualis::Library> m_library;
    std::unique_ptr<visualis::SongStore> m_store;
    QString m_error;
    QString m_search;
    std::optional<visualis::Song> m_selected;
    QString m_report;
};
