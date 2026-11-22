#pragma once

#include "song.h"
#include "songmatch.h"

#include <QList>
#include <QSqlDatabase>
#include <functional>
#include <optional>

namespace visualis {

class Library;

// Called by folder imports before each file, and once more at the end (done == total). Return
// false to stop: the import ends after the file in hand, reporting what it did so far.
using ImportProgress = std::function<bool(int done, int total)>;

struct SongSummary {
    QUuid id;
    QString title;
};

// Reads and writes songs in a Library. A song is saved whole, in one transaction, so a
// half-written song never reaches the database.
class SongStore
{
public:
    explicit SongStore(Library &library);

    struct Result {
        bool ok = true;
        QString error;    // for the operator
    };

    // Saves the song, its groups, and its arrangements. Gives new songs, groups, and
    // arrangements ids, and sets the timestamps. Rejects arrangements that use another
    // song's groups or a group that doesn't exist, and songs over the limits in songlimits.h.
    Result save(Song &song);
    std::optional<Song> load(const QUuid &id) const;
    // Updates only an existing song's Planning Center link and credits (pco_song_id,
    // ccli_number, authors, copyright): no lyrics, arrangements, or timestamps. Inside a
    // caller's transaction it lands or rolls back with it (#103), and alone it's a transaction
    // of its own. Refuses a song that isn't in the library.
    Result updatePcoLink(const Song &song);
    // Deletes the song with its groups and arrangements. False if there was no such song.
    bool remove(const QUuid &id);
    // Every song, sorted by title, ignoring case.
    QList<SongSummary> list() const;
    // Every song with what matching a Planning Center song needs (#55).
    QList<SongCandidate> matchCandidates() const;
    // Every title, in no order: cheaper than list() for duplicate checks.
    QStringList titles() const;

    // Several saves in one transaction, so they cost one disk sync (imports, #77). Each
    // save() inside it still lands or fails whole, but none is durable until commit(). Rolled
    // back if destroyed uncommitted.
    class Batch
    {
    public:
        explicit Batch(SongStore &store);
        ~Batch();
        Batch(const Batch &) = delete;
        Batch &operator=(const Batch &) = delete;
        Result commit();

    private:
        QSqlDatabase m_db;
        bool m_open = false;
    };

private:
    Library &m_library;
};

} // namespace visualis
