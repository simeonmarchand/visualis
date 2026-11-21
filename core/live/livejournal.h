#pragma once

#include "song/song.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <optional>

namespace visualis {

// What's live, saved on every change so a restart after a crash comes back to the same
// slide (plan 5.9 "live state journal", #25). The whole song is kept, not just its id, so
// recovery doesn't depend on the library opening.
struct LiveSnapshot
{
    std::optional<Song> song;   // none when no song is live
    QUuid arrangement;
    int slideIndex = -1;
    bool textCleared = false;
    bool backgroundCleared = false;
    QString background;         // the background video's URL, or empty
    QDateTime savedAt;          // UTC

    bool isEmpty() const { return !song && background.isEmpty(); }
};

QByteArray snapshotToJson(const LiveSnapshot &snapshot);

struct SnapshotResult
{
    std::optional<LiveSnapshot> snapshot;
    QString error;   // why it couldn't be read; the caller starts fresh
};
SnapshotResult snapshotFromJson(const QByteArray &json);

// Writes atomically (a temporary file renamed into place), so a crash mid-write leaves the
// previous journal, never half of one.
bool writeJournal(const QString &path, const LiveSnapshot &snapshot, QString *error = nullptr);
SnapshotResult readJournal(const QString &path);

} // namespace visualis
