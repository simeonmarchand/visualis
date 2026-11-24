#pragma once

#include "media/mediaasset.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QUuid>

#include <atomic>
#include <functional>
#include <optional>

namespace visualis {

// Turns a media asset id into a file to show, and says what's wrong when it can't (#98).
// GLOSSARY.md "Media snapshot", "Media check", "Media verification".

// The library's assets and media folder as a value, safe to read on any thread. It never
// touches the library's database.
struct MediaSnapshot {
    QString folder;   // the library's media/ folder, absolute
    QHash<QUuid, MediaAsset> assets;
};

MediaSnapshot snapshotOf(const QString &folder, const QList<MediaAsset> &assets);

enum class MediaStatus {
    Ok,
    UnknownId,      // no such asset in the library
    InvalidPath,    // its row's file isn't its hash's, or resolves outside the media folder
    Missing,        // no file there, an empty one, not a regular file, or (verifying) unreadable
    HashMismatch,   // verification only: the file isn't what was imported
    Undecodable,    // verification only: the file can't be decoded
    Stopped,        // verification only: the stop flag ended it before it finished
};

// What a verification found about the file, so an unchanged file needn't be hashed again.
struct KnownGood {
    qint64 size = -1;
    QDateTime modified;   // UTC

    bool operator==(const KnownGood &) const = default;
};

struct MediaCheck {
    MediaStatus status = MediaStatus::UnknownId;
    QString path;   // absolute, set whenever the file is there (even when it's damaged)
    MediaAsset::Kind kind = MediaAsset::Kind::Image;
    QString message;   // for the operator; empty when Ok
    std::optional<KnownGood> verified;   // verifyMedia, when Ok

    bool ok() const { return status == MediaStatus::Ok; }
};

// Whether a file decodes: empty when it does, else why not, for the operator. probeMedia in
// the app; a fake in tests.
using MediaDecode = std::function<QString(const QString &path, MediaAsset::Kind kind)>;
// checkMedia bound to wherever the snapshot comes from (MediaLibrary in the app).
using MediaCheckFn = std::function<MediaCheck(const QUuid &id)>;

// The quick check, for show time: the id is known, its path is valid and inside the media
// folder, and a non-empty regular file is there. One stat: no hashing, no decoding.
MediaCheck checkMedia(const MediaSnapshot &snapshot, const QUuid &id);

// The full check, for preflight: the quick check, then a rehash and `decode`. Given the
// record a previous verification returned for this asset (`knownGood`), a file of the same
// size and modification time isn't hashed again; it's still checked for and decoded. `stop`
// is checked between chunks of the hash. Safe on any thread.
MediaCheck verifyMedia(const MediaSnapshot &snapshot, const QUuid &id, const MediaDecode &decode,
                       const std::atomic_bool *stop = nullptr, const std::optional<KnownGood> &knownGood = std::nullopt);

} // namespace visualis
