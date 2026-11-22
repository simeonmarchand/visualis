#pragma once

#include "song.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace visualis {

struct PasteOptions {
    // Longer stanzas are split as evenly as possible. 2 matches the church's library (52% of
    // its ProPresenter slides) and pairs MultiTracks' short phrases into sentences (#14).
    // Belongs to the lyrics template once templates are stored (Phase 3).
    int maxLinesPerSlide = 2;
    QString title;              // a first line equal to this (pastes often start with it) is dropped
    // Reading back the song editor's own text (lyricsText()), which must round-trip: every
    // "[...]" line is a header with its label as written ("[]" is unlabelled), groups with
    // the same label are never merged, and there are no footer lines.
    bool editorText = false;
};

struct PastedLyrics {
    QList<Group> groups;        // new ids, default colors by kind
    QList<QUuid> sequence;      // the order as pasted, with repeats
    Credits credits;            // from SongSelect-style footer lines, if any
    QStringList warnings;       // for the operator
};

// Turns pasted lyrics into groups and slides (Phase 1 acceptance: "create a song by pasting
// lyrics with section labels; it auto-splits into slides").
//  - A line that is only a section label is a header: "Chorus", "Chorus:", "[Verse 2]",
//    "(Bridge)", "Pre-Chorus 1", with optional repeat marks "Chorus (x2)", "[Chorus x2]".
//  - A blank line starts a new slide within a group.
//  - Text before any header becomes one unlabelled group.
//  - A header seen again with no text, or the same text, repeats that group.
//  - SongSelect footer lines (CCLI Song #, the authors line after it, ©, license lines) are
//    taken out of the lyrics and kept as credits.
PastedLyrics parsePastedLyrics(const QString &text, const PasteOptions &options = {});

// A song from pasted lyrics. Adds one arrangement when the pasted order isn't simply each
// group once, in order.
Song songFromPastedLyrics(const QString &title, const QString &text, const PasteOptions &options = {},
                          QStringList *warnings = nullptr);

// The color a new group of this label gets (ProPresenter-like: verses blue, chorus red...).
QString defaultGroupColor(const QString &label);

} // namespace visualis
