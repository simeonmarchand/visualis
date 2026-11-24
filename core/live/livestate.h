#pragma once

#include "live/liveitem.h"

#include <QObject>

#include <optional>

namespace visualis {

// What is live right now: the one state every output renders (plan 5.2). The operator
// window changes it through the commands below; audience and stage windows only read it,
// so a reopened output window re-renders from here and loses nothing.
//
// Every change emits changed(). Outputs redraw the whole state instead of tracking deltas.
// It takes finished LiveItems (liveitem.h) and never sees templates (#93).
class LiveState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSlide READ hasSlide NOTIFY changed)
    Q_PROPERTY(QString songTitle READ songTitle NOTIFY changed)
    // The live song's id without braces, empty when nothing is live; lets a view tell
    // whether the song it shows is the live one.
    Q_PROPERTY(QString songId READ songId NOTIFY changed)
    Q_PROPERTY(int slideIndex READ slideIndex NOTIFY changed)
    Q_PROPERTY(int slideCount READ slideCount NOTIFY changed)
    Q_PROPERTY(QString currentText READ currentText NOTIFY changed)
    Q_PROPERTY(QString nextText READ nextText NOTIFY changed)
    Q_PROPERTY(QString groupLabel READ groupLabel NOTIFY changed)
    Q_PROPERTY(QString groupColor READ groupColor NOTIFY changed)
    Q_PROPERTY(bool textCleared READ textCleared NOTIFY changed)
    Q_PROPERTY(bool backgroundCleared READ backgroundCleared NOTIFY changed)
    // The live service and, when what's live came from it, the item (#51). Ids without
    // braces, empty when none; itemIndex is -1 when the live item isn't in the service.
    Q_PROPERTY(QString serviceId READ serviceIdString NOTIFY changed)
    Q_PROPERTY(QString itemId READ itemIdString NOTIFY changed)
    Q_PROPERTY(int itemIndex READ itemIndex NOTIFY changed)
    // The live slide's content layer, even while its text is cleared; empty with no slide.
    Q_PROPERTY(visualis::Content content READ content NOTIFY changed)
    // Why what's live is an older copy, empty when it isn't (see update()); for the operator.
    Q_PROPERTY(QString heldBack READ heldBack NOTIFY changed)

public:
    explicit LiveState(QObject *parent = nullptr);

    // Takes the item live at the given slide (liveSong() for a song from the library). Shows
    // the text again if it was cleared. Returns false, with no slide showing, when the index
    // is out of range or the item has no slides; the item is still live so the operator can
    // pick another slide. An item that isn't in the service (a library song) stops Next at
    // its last slide. An item with an error (a song over the size limits, songlimits.h)
    // isn't taken live at all: what's live stays, refused() says why, and it returns false.
    bool show(const LiveItem &item, int index = 0);

    // Puts back a state saved earlier (crash recovery, #25): the item at that slide with its
    // clears, and the service it came from, if any. An index that no longer fits leaves the
    // item live with no slide, as show() does. setService() with the same service afterwards
    // picks the service back up at that item. An item with an error is refused as in show(),
    // leaving everything as it was; returns false then.
    bool restore(const LiveItem &item, int index, bool textCleared, bool backgroundCleared, const QUuid &service = {});

    // Swaps in a new rendering of the live item (after its song or template changed), keeping
    // the slide index, clamped to the slides it has now. The live item is the one with the
    // same service item id, or, for a library song, the same song id. Returns false, changing
    // nothing, when `item` isn't the live one. A rendering that can't show (its song deleted,
    // or over the limits) never blanks the output: the copy on screen stays, and heldBack()
    // says why until another item goes live.
    bool update(const LiveItem &item);

    // The service being run, one LiveItem per service item in order (#51). Doesn't change
    // which item is showing: when the live item is among `items` the service carries on from
    // it (after a reorder, an edit, or a recovery) with its new rendering, as update() does;
    // otherwise Next and Previous stay in what's live until an item is taken live. Call again
    // after every change to the service, its songs, or their templates.
    void setService(const QUuid &id, const QList<LiveItem> &items);
    void clearService() { setService({}, {}); }
    const QList<LiveItem> &serviceItems() const { return m_items; }

    // Takes item n of the service live at slide `slide` (-1: its last slide). False, with
    // nothing changed, when there's no such item or slide; headers have no slides.
    bool goToItem(int n, int slide = 0);

    // Step through the slides, crossing groups. In a service, Next at an item's last slide
    // goes to the first slide of the next item that has slides, and Previous at an item's
    // first slide to the last slide of the one before; with nothing live, Next starts the
    // service. At either end nothing changes and they return false. A step shows the text
    // again if it was cleared.
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();
    // Also brings back cleared text when n is the current slide.
    Q_INVOKABLE bool goToSlide(int n);
    // Jumps to the start of the next group whose label starts with `letter` (ignoring
    // case), searching ahead of the current slide and wrapping to the start, so pressing it
    // again steps Verse 1 -> Verse 2 and "go to Chorus" works when the arrangement doesn't
    // have it next. False when no group in the arrangement starts with it.
    Q_INVOKABLE bool goToGroupStartingWith(const QString &letter);

    // Clearing keeps the song and position, so Next carries on from where it was.
    Q_INVOKABLE void clearText();
    Q_INVOKABLE void clearBackground();
    Q_INVOKABLE void clearAll();
    // Brings a cleared background back (plan 5.8 show_background).
    Q_INVOKABLE void showBackground();

    const LiveItem &item() const { return m_live; }   // what's live, whole
    const Song &song() const { return m_live.song; }
    QUuid arrangementId() const { return m_live.arrangement; }   // null for the default
    const QList<LiveSlide> &slides() const { return m_live.slides; }
    const SongSlide *currentSlide() const;   // null when no slide is showing
    Content content() const;

    bool hasSlide() const { return currentSlide() != nullptr; }
    QString songTitle() const { return m_live.song.title; }
    QString songId() const { return braceless(m_live.song.id); }
    int slideIndex() const { return m_index; }   // -1 when no slide is showing
    int slideCount() const { return int(m_live.slides.size()); }
    // The slide's text even while cleared: each output decides what clearing hides
    // (the audience shows nothing; a stage display may keep it).
    QString currentText() const;
    QString nextText() const;   // empty at the last slide
    QString groupLabel() const;
    QString groupColor() const;
    bool textCleared() const { return m_textCleared; }
    bool backgroundCleared() const { return m_backgroundCleared; }
    QUuid serviceId() const { return m_serviceId; }
    QUuid itemId() const { return m_live.id(); }
    QString serviceIdString() const { return braceless(m_serviceId); }
    QString itemIdString() const { return braceless(m_live.id()); }
    int itemIndex() const;
    QString heldBack() const { return m_heldBack; }

signals:
    void changed();
    // A song wasn't taken live because it's too big to expand; for the operator.
    void refused(const QString &reason);

private:
    bool moveTo(int n);
    // False, after refused(), when the item can't be shown.
    bool acceptable(const LiveItem &item);
    // Swaps in `item` as the live one, clamping the slide index to its slides.
    void replaceLive(const LiveItem &item);
    void showItem(int n, int slide);
    static QString braceless(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }

    LiveItem m_live;   // its id is the service item's, null for a library song
    int m_index = -1;
    bool m_textCleared = false;
    bool m_backgroundCleared = false;
    QString m_heldBack;
    QUuid m_serviceId;
    QList<LiveItem> m_items;
};

} // namespace visualis
