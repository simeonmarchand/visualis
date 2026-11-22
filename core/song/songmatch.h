#pragma once

#include "pco/pcotypes.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>
#include <optional>

namespace visualis {

// What the matcher needs to know about a library song (SongStore::matchCandidates).
struct SongCandidate {
    QUuid id;
    QString title;
    QString ccliNumber;
    QString authors;
    QString pcoSongId;

    bool operator==(const SongCandidate &) const = default;
};

// Which library song a Planning Center song is (#55, plan 5.3).
struct SongMatch {
    enum class Kind {
        Linked,       // sure enough to use
        Confirm,      // a likely song, for the operator to confirm
        None,         // nothing close: a placeholder
    };
    enum class By { Nothing, PcoSongId, Ccli, Title, SimilarTitle };

    Kind kind = Kind::None;
    By by = By::Nothing;
    QUuid songId;             // Linked: the song
    QList<QUuid> candidates;  // Confirm: likely songs, best first (at most 3)

    bool operator==(const SongMatch &) const = default;
};

// Matches in plan 5.3's order, and never guesses between two songs:
//  1. the library song already linked to this Planning Center song;
//  2. the one song with this CCLI number;
//  3. the one song whose title is the same once case, accents, punctuation, "&"/"and", a
//     leading "The", and a trailing recording note ("(Live)", "(Acoustic)", "(feat. …)") are
//     set aside: linked "by title"
//     (Simeon's decision on #55: week one has nothing else to go on). Not if that song is
//     already linked to another Planning Center song or has another CCLI number;
//  4. otherwise similar titles, or several equal ones, for the operator to confirm, the
//     author's name breaking ties;
//  5. otherwise nothing.
SongMatch matchSong(const PcoSong &song, const QList<SongCandidate> &library);

// A title folded for comparing: "The Lord's Prayer (Live)" -> "lords prayer".
QString normalizedTitle(const QString &title);

// The Planning Center song in an import snapshot (planimport.h), if the item had one.
std::optional<PcoSong> snapshotSong(const QJsonObject &importSnapshot);

} // namespace visualis
