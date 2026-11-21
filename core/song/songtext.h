#pragma once

#include "song.h"

#include <QString>
#include <QStringList>

namespace visualis {

// A song's lyrics as one editable text (plan 5.4: text-first editing): each group starts
// with a "[Label]" line, and a blank line separates slides. A group with no lyrics
// (an Intro) is just its header.
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
LyricsEdit applyLyricsText(const Song &song, const QString &text);

} // namespace visualis
