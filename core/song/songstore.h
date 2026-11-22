#pragma once

#include "song.h"

#include <QList>
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
    // song's groups or a group that doesn't exist.
    Result save(Song &song);
    std::optional<Song> load(const QUuid &id) const;
    // Deletes the song with its groups and arrangements. False if there was no such song.
    bool remove(const QUuid &id);
    // Every song, sorted by title, ignoring case.
    QList<SongSummary> list() const;

private:
    Library &m_library;
};

} // namespace visualis
