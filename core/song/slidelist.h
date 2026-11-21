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
QList<SongSlide> songSlides(const Song &song, const QUuid &arrangement = {});

} // namespace visualis
