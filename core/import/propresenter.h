#pragma once

#include "song/song.h"

#include <QString>
#include <QStringList>
#include <optional>

namespace visualis {

class SongStore;

namespace propresenter {

struct ReadResult {
    std::optional<Song> song;   // groups, slides, arrangements, credits; fresh ids; not yet saved
    bool looksLikeSong = false; // has section groups (Verse, Chorus...), not a sermon or countdown
    QString error;              // set when the file can't be read
    QStringList warnings;
};

// Reads a ProPresenter 7 presentation (.pro). The title comes from the file name, as
// ProPresenter shows it (the name stored inside is often "Untitled"). The format is
// undocumented protobuf; field numbers follow the MIT-licensed reverse-engineered schema at
// github.com/greyshirtguy/ProPresenter7-Proto.
ReadResult readFile(const QString &path);
ReadResult readBytes(const QByteArray &bytes, const QString &title);

struct ImportReport {
    QStringList imported;                     // titles
    QList<std::pair<QString, QString>> skipped;  // file name, reason
    QList<std::pair<QString, QString>> failed;   // file name, error
    QStringList warnings;                     // "file: warning"
};

// Imports every .pro file in `folder` that looks like a song. Skips presentations that aren't
// songs and songs whose title is already in the library, so running it twice adds nothing.
// A file that can't be read is reported and the rest carry on.
ImportReport importFolder(const QString &folder, SongStore &store);

} // namespace propresenter
} // namespace visualis
