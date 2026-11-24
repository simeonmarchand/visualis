#pragma once

#include "song.h"

#include <QString>
#include <QStringList>

namespace visualis {

// A song's lyrics as one editable text (plan 5.4: text-first editing): each group starts
// with a "[Label]" line ("[]" when it has no label), and a blank line separates slides. A
// group with no lyrics (an Intro) is just its header. Any "[...]" line reads back as a
// header, so custom labels ("[Spoken Word]") survive the round trip. A blank slide among
// others is a "(blank)" line of its own. A lyric line that would read back as a header or
// that marker is written with a leading "\\".
QString lyricsText(const Song &song);

struct LyricsEdit {
    Song song;
    QStringList warnings;   // for the operator
};

// Applies edited lyrics text to a song. Slides are taken exactly as written (no automatic
// splitting). Groups keep their ids and colors, so arrangements stay intact:
//  - a header with an existing group's label is that group (ignoring case);
//  - a new header where a group disappeared at the same position is that group renamed;
//  - other new headers are new groups, with the default color for their label;
//  - groups whose header is gone are removed, along with their arrangement steps.
// A matched group's per-slide overrides follow their slides, since the text has no slide
// identity (ADR 0002):
//  1. a slide whose text exactly equals an unused old slide of the group takes its override,
//     in order, so reordered or repeated slides keep theirs;
//  2. then, if the group has as many slides as before, the rest match by position, so
//     fixing a typo keeps the slide's style;
//  3. any other override is dropped, with one warning per group saying how many.
LyricsEdit applyLyricsText(const Song &song, const QString &text);

} // namespace visualis
