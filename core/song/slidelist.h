#pragma once

#include "song.h"

#include <QList>
#include <QString>
#include <QUuid>

namespace visualis {

// One slide as the operator steps through it: the text plus where it sits in the song.
struct SongSlide {
    QString text;           // empty for a blank slide (Intro, Interlude)
    QUuid groupId;
    QString groupLabel;
    QString groupColor;     // "#rrggbb" or empty
    int step = 0;           // index into the arrangement's sequence: tells repeats of a group apart
    int indexInGroup = 0;   // 0-based
    int groupSlideCount = 1;

    bool isBlank() const { return text.trimmed().isEmpty(); }
    bool operator==(const SongSlide &) const = default;
};

// The slides to step through for a song, in order (Song::playOrder picks the arrangement).
//  - A repeated group appears again at each step that uses it.
//  - A group with no slides gives one blank slide, so there's still something to land on.
//  - A step whose group no longer exists is skipped.
//  - An arrangement with no steps gives no slides; the caller should tell the operator.
//  - A song over songlimits (songlimits.h), or that would expand to more than
//    kPlayedSlides, gives no slides and says why in `error`; nothing large is allocated.
QList<SongSlide> songSlides(const Song &song, const QUuid &arrangement = {}, QString *error = nullptr);

// The slides for a song played in an order given by label, such as a service's Planning
// Center sequence (ServiceItem::customSequence). Labels are matched to groups with
// matchSequence (labels.h). A label for a section that has no lyrics (Intro, Turnaround,
// Interlude, Ending...) and no group of its own gives one blank slide, labelled as given,
// with a null group id (P3 finding 9). Other unmatched labels are skipped; the import
// lists them for the operator (#55).
QList<SongSlide> songSlides(const Song &song, const QStringList &sequence, QString *error = nullptr);

// Whether a label names a section that has no lyrics: Intro, Interlude, Ending, Blank...
bool isLyricless(const QString &label);

} // namespace visualis
