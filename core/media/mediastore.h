#pragma once

#include "media/mediaasset.h"

#include <QList>
#include <QSet>
#include <QStringList>
#include <optional>

namespace visualis {

class Library;

// Reads and writes media assets in a Library, as SongStore does songs. Only the database
// and deleting an asset's files: imports publish files with publishPartial (mediafiles.h).
//
// Writes don't wait for the library's write lock: when another program holds it they fail at
// once with `busy` set, and the caller retries later (as PlanImporter does, #88), so the GUI
// thread never stalls on it.
class MediaStore
{
public:
    explicit MediaStore(Library &library);

    struct Result {
        bool ok = true;
        QString error;      // for the operator
        bool busy = false;  // another program holds the library's write lock; try again
    };

    // Inserts a new asset (giving it an id and a created time) or updates an existing one.
    // Refuses a second asset with the same content hash, and a file that isn't the hash's.
    Result save(MediaAsset &asset);
    // Loading checks each row: one whose file isn't exactly its hash's path ("../..", an
    // absolute path) comes back with an empty `file`, so nothing is ever opened or deleted
    // outside the media folder.
    std::optional<MediaAsset> load(const QUuid &id) const;
    std::optional<MediaAsset> findByHash(const QString &hash) const;
    // Every asset, sorted by original name, ignoring case.
    QList<MediaAsset> list() const;
    // The titles of the services with an item showing the asset, so deleting it can ask first.
    // TODO(#99): also count backgrounds set as song and service defaults and as item overrides.
    QStringList usedBy(const QUuid &id) const;
    // Deletes the asset's row, then its file and thumbnail if no other row uses them.
    Result remove(const QUuid &id);
    // Whether any row stores this file (relative to media/).
    bool isReferenced(const QString &file) const;
    // Every row's content hash, or nothing if the database couldn't be read: a sweep given a
    // partial set would delete files that are in use (#114).
    std::optional<QSet<QString>> contentHashes() const;

    // Absolute paths inside the media folder, or empty when the row's file isn't valid or
    // would resolve outside it (containedPath). Checked again on every call: the folders can
    // change under a running app.
    QString filePath(const MediaAsset &asset) const;
    QString thumbnailPath(const MediaAsset &asset) const;

private:
    Library &m_library;
};

} // namespace visualis
