#pragma once

#include "media/mediaasset.h"

#include <QSet>
#include <QString>
#include <atomic>
#include <chrono>
#include <optional>

namespace visualis {

// The files of the library's media/ folder (plan 5.9, #97). A file is stored under its
// SHA-256, as "ab/abcdef….ext". Importing is two steps. A worker copies into .partial/
// (copyToPartial). Then the one thread that writes the library publishes the copy into place
// (publishPartial) and saves its row, so deciding what may be deleted happens in one place.

// The kind a file's name says it is, or nothing if it's neither an image nor a video
// Visualis can show. Probing confirms it.
std::optional<MediaAsset::Kind> mediaKindFor(const QString &fileName);

struct PartialCopy {
    QString hash;        // SHA-256 of what was copied, lower-case hex
    QString extension;   // the source's, lower case, or empty
    QString path;        // absolute: media/.partial/<uuid>[.ext]
    bool cancelled = false;
    QString error;       // for the operator; empty on success
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Copies `source` into media/.partial/, hashing as it copies so the file is read once. The
// copy keeps the source's extension, since media frameworks choose a reader by it. `stop` is
// checked between chunks, and a stopped or failed copy removes its file. Files over `maxBytes`
// are refused before anything is copied. Safe to call from any thread.
PartialCopy copyToPartial(const QString &source, const QString &mediaFolder, qint64 maxBytes,
                          const std::atomic_bool *stop = nullptr);

// media/.partial/ as an absolute path, created if `create`; empty if it's missing or isn't the
// library's own folder (a link elsewhere). Copies are staged there, inside media/, so
// publishing is a rename on one filesystem.
QString partialFolder(const QString &mediaFolder, bool create);

// A new, unique path in media/.partial/ ending in `suffix` (e.g. ".jpg"), for a file that's
// published later.
QString newPartialPath(const QString &mediaFolder, const QString &suffix);

// Where a hash's file goes, relative to media/: "ab/<hash>.<ext>".
QString mediaFileFor(const QString &hash, const QString &extension);
// Where its thumbnail goes, relative to media/.
QString thumbnailFile(const QString &hash);

// Whether `hash` is a SHA-256 in lower-case hex, and `file` is exactly the relative path
// mediaFileFor() gives for it. Anything else in the database ("../..", an absolute path) is
// never joined to the media folder.
bool isValidHash(const QString &hash);
bool isValidMediaFile(const QString &file, const QString &hash);

// The absolute path of `relative` (already validated) inside the media folder, or empty when
// it would resolve outside it: when the folder it's in, or the file itself, is a symbolic link.
// The media folder itself may be a link (to an external drive, say).
QString containedPath(const QString &mediaFolder, const QString &relative);

struct Publish {
    bool existed = false;   // the same content was already in place; the partial was dropped
    QString error;          // empty on success
    bool ok() const { return error.isEmpty(); }
};

// Moves a partial copy to `relative` (validated) in the media folder, by a rename that never
// falls back to copying: one across volumes fails instead. When a file is already
// there of the same size, it's the same content (the name is the hash). That's another import
// winning a race, or a file left by a crash. The partial is then dropped and `existed` is set.
Publish publishPartial(const QString &partialPath, const QString &mediaFolder, const QString &relative);

// Removes partial copies not written to for `olderThan`: those a crash left. A copy in
// progress, by this or another Visualis, is written to every few milliseconds.
void removeStalePartialCopies(const QString &mediaFolder, std::chrono::seconds olderThan);

// Removes published files and thumbnails whose hash isn't in `inUse` (every row's content
// hash): those a crash left between publishing a copy and saving its row (#114). Only names
// Visualis gives files are touched ("ab/<hash>[.ext]", "thumbs/<hash>.jpg"). Links, and
// anything a link points at, are left alone, as is everything else in the folder. The caller
// must pass every hash in the library: a missing one deletes that asset's file.
void removeUnusedMediaFiles(const QString &mediaFolder, const QSet<QString> &inUse);

} // namespace visualis
