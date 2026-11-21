#pragma once

#include "song.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <optional>

namespace visualis {

// A group label taken apart: "Spanish - Pre-Chorus 2" -> kind "prechorus", number 2,
// language "spanish". Spellings, case, accents, and small typos are folded together.
struct SectionLabel {
    QString kind;               // a known kind ("verse", "prechorus", ...), else the cleaned text
    std::optional<int> number;
    QString language;           // "" when the label doesn't name one
    bool known = false;         // whether `kind` is a recognized section kind

    bool operator==(const SectionLabel &) const = default;
};

SectionLabel parseLabel(const QString &text);

struct SequenceMatch {
    QList<QUuid> steps;         // one per input label: the matched group, or a null id
    QStringList unmatched;      // labels with no match, each once, in first-seen order
};

// Matches labels from another source (a Planning Center sequence, pasted headers) to a song's
// groups. Forgiving about spelling and numbering, never guesses:
//  - same kind and number;
//  - otherwise, if the song has only one group of that kind and either side is unnumbered,
//    that group ("Pre Chorus 1" -> "PreChorus", "Chorus" -> "Chorus 1");
//  - otherwise "X 1" matches an unnumbered "X", and "X" matches "X 1";
//  - anything ambiguous or missing stays unmatched for the operator.
// Only groups in `language` are considered ("" for groups with no language tag).
SequenceMatch matchSequence(const QStringList &labels, const QList<Group> &groups, const QString &language = {});

} // namespace visualis
