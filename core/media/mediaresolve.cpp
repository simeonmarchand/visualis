#include "media/mediaresolve.h"

#include "media/mediafiles.h"

#include <QFileInfo>
#include <QTimeZone>

namespace visualis {

MediaSnapshot snapshotOf(const QString &folder, const QList<MediaAsset> &assets)
{
    MediaSnapshot out{folder, {}};
    out.assets.reserve(assets.size());
    for (const MediaAsset &a : assets)
        out.assets.insert(a.id, a);
    return out;
}

MediaCheck checkMedia(const MediaSnapshot &snapshot, const QUuid &id)
{
    MediaCheck out;
    const auto found = snapshot.assets.constFind(id);
    if (found == snapshot.assets.constEnd()) {
        out.message = QStringLiteral("This media is no longer in the library.");
        return out;
    }
    const MediaAsset &asset = *found;
    out.kind = asset.kind;
    const QString path = isValidMediaFile(asset.file, asset.contentHash) ? containedPath(snapshot.folder, asset.file)
                                                                         : QString();
    if (path.isEmpty()) {
        out.status = MediaStatus::InvalidPath;
        out.message = QStringLiteral("\"%1\" can't be shown: the library's record of its file is damaged.")
                          .arg(asset.originalName);
        return out;
    }
    const QFileInfo file(path);
    if (!file.isFile() || file.size() == 0) {
        out.status = MediaStatus::Missing;
        out.message = QStringLiteral("\"%1\" is missing from the media folder. Import it again.").arg(asset.originalName);
        return out;
    }
    out.status = MediaStatus::Ok;
    out.path = path;
    return out;
}

MediaCheck verifyMedia(const MediaSnapshot &snapshot, const QUuid &id, const MediaDecode &decode,
                       const std::atomic_bool *stop, const std::optional<KnownGood> &knownGood)
{
    MediaCheck out = checkMedia(snapshot, id);
    if (!out.ok())
        return out;
    const MediaAsset &asset = snapshot.assets[id];
    const QFileInfo file(out.path);
    const KnownGood now{file.size(), file.lastModified(QTimeZone::UTC)};
    if (now != knownGood) {
        const FileHash hash = hashFile(out.path, stop);
        if (hash.cancelled) {
            out.status = MediaStatus::Stopped;
            out.message = QStringLiteral("Checking \"%1\" was stopped.").arg(asset.originalName);
            return out;
        }
        if (!hash.ok() || hash.hash != asset.contentHash) {
            out.status = MediaStatus::HashMismatch;
            out.message = hash.ok() ? QStringLiteral("\"%1\" has changed or is damaged on disk. Import it again.")
                                          .arg(asset.originalName)
                                    : QStringLiteral("\"%1\" can't be read: %2").arg(asset.originalName, hash.error);
            return out;
        }
    }
    if (const QString why = decode(out.path, asset.kind); !why.isEmpty()) {
        out.status = MediaStatus::Undecodable;
        out.message = QStringLiteral("\"%1\" can't be shown: %2").arg(asset.originalName, why);
        return out;
    }
    out.verified = now;
    return out;
}

} // namespace visualis
