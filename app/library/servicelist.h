#pragma once

#include "live/liveitem.h"
#include "service/service.h"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

class QJSEngine;
class QQmlEngine;
class SongLibrary;
namespace visualis {
class LiveState;
class ServiceStore;
}

// The operator window's services (#51, W9): the list of services, the open one's items,
// and the selected item's slides for the grid. The open service is the one LiveState runs,
// so Next at an item's last slide carries on into the next item. Every edit saves to the
// library at once, as songs do, and is handed to LiveState without moving what's live.
class ServiceList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // {id, title, date} for every service, newest date first.
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QString openId READ openId NOTIFY serviceChanged)
    Q_PROPERTY(QString openTitle READ openTitle NOTIFY serviceChanged)
    // Whether the open service came from a Planning Center plan, so it can be re-imported.
    Q_PROPERTY(bool openImported READ openImported NOTIFY serviceChanged)
    // {id, kind, title, hasSlides, songPlaceholder, suggestions, pcoRemoved} per item of the
    // open service, in order. `songPlaceholder`: an imported song not linked to the library
    // yet; `suggestions`: [{id, title}] of library songs it may be (#55); `pcoRemoved`: taken
    // out of the plan in Planning Center (#56).
    Q_PROPERTY(QVariantList items READ items NOTIFY serviceChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    // The selected item's song, for the editor; empty for other kinds.
    Q_PROPERTY(QString selectedSongId READ selectedSongId NOTIFY selectionChanged)
    // {text, groupLabel, groupColor} per slide of the selected item, as LiveState steps them.
    Q_PROPERTY(QVariantList selectedSlides READ selectedSlides NOTIFY selectionChanged)
    // Whether the slide grid shows the selected item (true) or the library's song (false):
    // whichever the operator picked last, or the item that just went live.
    Q_PROPERTY(bool inGrid READ inGrid NOTIFY selectionChanged)
    // What the last change or import did, when it's worth saying.
    Q_PROPERTY(QString report READ report NOTIFY reportChanged)

public:
    ServiceList(SongLibrary *songs, visualis::LiveState *live, QObject *parent = nullptr);
    ~ServiceList() override;
    static ServiceList *create(QQmlEngine *qml, QJSEngine *);

    Q_INVOKABLE bool createService(const QString &title);
    Q_INVOKABLE bool openService(const QString &id);
    Q_INVOKABLE bool renameService(const QString &title);
    // Deletes a service. What's live keeps showing; if it was this service, Next stops at
    // the end of the live item.
    Q_INVOKABLE bool removeService(const QString &id);

    // New items go after the selected one (at the end when nothing is selected) and are
    // selected.
    Q_INVOKABLE bool addSong(const QString &songId);
    Q_INVOKABLE bool addHeader(const QString &title);
    // A text item; an empty body is a blank slide.
    Q_INVOKABLE bool addText(const QString &title, const QString &body);
    Q_INVOKABLE bool removeItem(int index);
    // Resolving a placeholder (#55). Linking an imported song remembers its Planning Center
    // id on the library song, so it matches at once next time.
    Q_INVOKABLE bool linkSong(int index, const QString &songId);
    // Keeps the item's place as one blank slide.
    Q_INVOKABLE bool leaveBlank(int index);
    // Moves the item at `from` to `to` (both indexes before the move); the moved item stays
    // selected if it was.
    Q_INVOKABLE bool moveItem(int from, int to);
    Q_INVOKABLE void select(int index);
    // The library's song is in the grid now, not the service item.
    Q_INVOKABLE void showLibrarySong();

    // Every *.visualis-service.json in a folder, as exported (plan 5.9). Services already in
    // the library are skipped. Opens the last one imported. Songs should be imported first,
    // or their items load as placeholders.
    Q_INVOKABLE void importServices(const QUrl &folder);
    // A service another part of the app saved to the library (a Planning Center import,
    // #54): lists it and opens it, as creating one does.
    bool openAdded(const QString &id);

    QVariantList services() const;
    QString openId() const;
    QString openTitle() const;
    bool openImported() const { return m_open && !m_open->pcoPlanId.isEmpty(); }
    QVariantList items() const;
    int selectedIndex() const { return m_selected; }
    QString selectedTitle() const;
    QString selectedSongId() const;
    QVariantList selectedSlides() const;
    bool inGrid() const { return m_inGrid; }
    QString report() const { return m_report; }

signals:
    void servicesChanged();
    void serviceChanged();
    void selectionChanged();
    void reportChanged();

private:
    void libraryChanged();
    bool insert(visualis::ServiceItem item);
    bool save();
    // Rebuilds the items' slides from the library and hands the service to LiveState.
    void refresh();
    void followLive();
    void setReport(const QString &report);

    QPointer<SongLibrary> m_songs;
    QPointer<visualis::LiveState> m_live;
    std::unique_ptr<visualis::ServiceStore> m_store;
    std::optional<visualis::Service> m_open;
    QList<visualis::LiveItem> m_liveItems;   // one per item of m_open
    QList<QVariantList> m_suggestions;       // one per item of m_open
    int m_selected = -1;
    bool m_inGrid = false;
    QUuid m_lastLiveItem;
    QString m_report;
};
