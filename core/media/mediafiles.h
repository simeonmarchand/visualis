#pragma once

#include "media/mediaasset.h"

#include <QString>
#include <atomic>
#include <optional>

namespace visualis {

// The kind a file's name says it is, or nothing if it's neither an image nor a video
// Visualis can show. Probing confirms it.
std::optional<MediaAsset::Kind> mediaKindFor(const QString &fileName);

struct MediaCopy {
    QString hash;        // SHA-256 of what was copied
    QString file;        // relative to the media folder
    bool existed = false;   // the same content was already there
    bool cancelled = false;
    QString error;       // for the operator; empty on success
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Copies `source` into `mediaFolder` under its content hash, hashing as it copies so the file
// is read once. The copy goes to .partial/ first and is renamed into place only when complete,
// so a cancelled, failed, or crashed copy never leaves a file that looks finished. `stop` is
// checked between chunks; a stopped copy removes its partial file. Files over `maxBytes` are
// refused before anything is copied. Safe to call from any thread.
MediaCopy copyIntoMedia(const QString &source, const QString &mediaFolder, qint64 maxBytes,
                        const std::atomic_bool *stop = nullptr);

// Where a file's thumbnail goes, relative to the media folder.
QString thumbnailFile(const QString &hash);

// Removes partial copies left by a crash. Call when no import is running into the folder.
void removePartialCopies(const QString &mediaFolder);

} // namespace visualis
