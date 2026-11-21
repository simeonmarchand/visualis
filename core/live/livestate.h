#pragma once

#include "song/slidelist.h"

#include <QObject>

namespace visualis {

// What is live right now: the one state every output renders (plan 5.2). The operator
// window changes it through the commands below; audience and stage windows only read it,
// so a reopened output window re-renders from here and loses nothing.
//
// Every change emits changed(). Outputs redraw the whole state instead of tracking deltas.
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

public:
    explicit LiveState(QObject *parent = nullptr);

    // Takes the song live at the given slide of the arrangement (Song::playOrder picks
    // it). Shows the text again if it was cleared. Returns false, with no slide showing,
    // when the index is out of range or the arrangement has no slides; the song is still
    // live so the operator can pick another slide.
    bool show(const Song &song, const QUuid &arrangement = {}, int index = 0);

    // Puts back a state saved earlier (crash recovery, #25): the song at that slide with its
    // clears. An index that no longer fits leaves the song live with no slide, as show() does.
    void restore(const Song &song, const QUuid &arrangement, int index, bool textCleared, bool backgroundCleared);

    // Step through the slides, crossing groups. At either end nothing changes and they
    // return false. A step shows the text again if it was cleared.
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

    const Song &song() const { return m_song; }
    QUuid arrangementId() const { return m_arrangement; }   // as passed to show(); null for the default
    const QList<SongSlide> &slides() const { return m_slides; }
    const SongSlide *currentSlide() const;   // null when no slide is showing

    bool hasSlide() const { return currentSlide() != nullptr; }
    QString songTitle() const { return m_song.title; }
    QString songId() const { return m_song.id.isNull() ? QString() : m_song.id.toString(QUuid::WithoutBraces); }
    int slideIndex() const { return m_index; }   // -1 when no slide is showing
    int slideCount() const { return int(m_slides.size()); }
    // The slide's text even while cleared: each output decides what clearing hides
    // (the audience shows nothing; a stage display may keep it).
    QString currentText() const;
    QString nextText() const;   // empty at the last slide
    QString groupLabel() const;
    QString groupColor() const;
    bool textCleared() const { return m_textCleared; }
    bool backgroundCleared() const { return m_backgroundCleared; }

signals:
    void changed();

private:
    bool moveTo(int n);

    Song m_song;
    QUuid m_arrangement;
    QList<SongSlide> m_slides;
    int m_index = -1;
    bool m_textCleared = false;
    bool m_backgroundCleared = false;
};

} // namespace visualis
