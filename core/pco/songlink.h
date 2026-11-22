#pragma once

#include "pco/pcotypes.h"
#include "service/service.h"
#include "song/song.h"
#include "song/songmatch.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace visualis {

class SongStore;

// Turns an imported song placeholder into a song item for `song` (#55): the song, its
// arrangement linked to the item's Planning Center arrangement if it has one, and the
// item's sequence and snapshot kept as they were.
ServiceItem linkedItem(ServiceItem item, const Song &song);

// Remembers which Planning Center song a library song is, so it matches at once next time,
// and fills in credits the song doesn't have (ProPresenter files carry none). Returns
// whether anything changed. A song already linked to another Planning Center song keeps
// its link.
bool rememberPcoSong(Song &song, const PcoSong &pco);
// The operator says this is the song: it takes the Planning Center id even from another
// link, and fills in credits it has none of. Returns whether anything changed. Whoever held
// the id before must let go of it (releasePcoSong), or next week's import can't decide.
bool takePcoSong(Song &song, const PcoSong &pco);
// Clears the id from a song that holds it. Returns whether it did.
bool releasePcoSong(Song &song, const QString &pcoSongId);

// The labels in an item's sequence that name no group of the song and aren't lyric-less
// (Intro, Ending): what the operator should look at ("Bridge 2" when the song has only
// "Bridge 1"; ProPresenter spike finding 8).
QStringList unmatchedLabels(const ServiceItem &item, const Song &song);

// What linking an imported service's songs did, for the import summary.
struct SongLinkReport {
    int linked = 0;          // by Planning Center id or CCLI
    int byTitle = 0;         // linked by title alone: worth a glance
    int toConfirm = 0;       // a suggestion is waiting for the operator
    int placeholders = 0;    // nothing close in the library
    QStringList unmatched;   // "Song title: Bridge 2, Bridge 3", one per song with any

    // "4 songs matched (1 by title), 1 to confirm, 1 not in the library." Empty with no songs.
    QString summary() const;
};

// Links every song placeholder in `service` that matchSong() is sure of, saving each
// library song's Planning Center id. Doesn't save the service.
SongLinkReport linkSongs(Service &service, SongStore &songs);

} // namespace visualis
