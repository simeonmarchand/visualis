#pragma once

#include "song.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

#include "songimport.h"
#include "songstore.h"

namespace visualis {

// Songs as human-readable JSON (plan 5.9), so a church's songs never depend on this app's
// database. One file per song, "<title>.visualis-song.json". Also the test fixture format.
//
// {
//   "format": "visualis-song", "version": 1,
//   "id": "…", "title": "Amazing Grace",
//   "credits": {"ccliNumber": "", "authors": "John Newton", "copyright": "Public Domain"},
//   "pcoSongId": "",
//   "groups": [{"id": "…", "label": "Verse 1", "color": "#0078cc", "slides": ["line\nline", "…"],
//               "slideOverrides": [{}, {"regions": {"main": {"color": "#ffcc00"}}}]}],   // optional
//   "defaultTemplate": "<template id>",                                                // optional
//   "arrangements": [{"id": "…", "name": "Default", "sequence": ["<group id or label>", …],
//                     "pcoArrangementId": ""}],
//   "defaultArrangement": "<arrangement id or name>",
//   "created": "2026-11-21T15:04:05.000Z", "updated": "…"
// }
//
// Written by hand (fixtures), ids may be left out and sequences may name groups by label.
QByteArray songToJson(const Song &song);

struct SongJsonResult {
    std::optional<Song> song;
    QString error;   // set when the JSON can't be read as a song
};
SongJsonResult songFromJson(const QByteArray &json);

// The file name a song exports to, with characters that aren't allowed in file names replaced.
// The title part is at most 120 UTF-8 bytes, cut between characters, never inside one.
QString songFileName(const QString &title);

struct SongExportReport {
    QStringList written;                         // file names
    QList<std::pair<QString, QString>> failed;   // title, error
};
// Writes every song in the library to `folder`, one file each. Titles that collide get " (2)".
SongExportReport exportSongs(SongStore &store, const QString &folder);

// Imports every *.visualis-song.json in `folder`. Never overwrites: a song whose id is already
// in the library is skipped. A title already in the library under another id is imported with
// a warning, since it may be a different song.
// Files over kMaxSongFileBytes are skipped unread: a song is a few KB.
constexpr qint64 kMaxSongFileBytes = 4 * 1024 * 1024;
// The two steps of importSongs, for an import split across threads (see songimport.h).
QFileInfoList songFiles(const QString &folder);
ImportFile readSongFile(const QFileInfo &file);
SongImportReport importSongs(const QString &folder, SongStore &store, const ImportProgress &progress = {});

} // namespace visualis
