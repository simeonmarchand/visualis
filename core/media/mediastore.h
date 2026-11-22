#pragma once

#include "media/mediaasset.h"

#include <QList>
#include <QStringList>
#include <optional>

namespace visualis {

class Library;

// Reads and writes media assets in a Library, as SongStore does songs. Only the database:
// copying files in is copyIntoMedia's job, on a worker thread.
class MediaStore
{
public:
    explicit MediaStore(Library &library);

    struct Result {
        bool ok = true;
        QString error;   // for the operator
    };

    // Inserts a new asset (giving it an id and a created time) or updates an existing one.
    // Refuses a second asset with the same content hash.
    Result save(MediaAsset &asset);
    std::optional<MediaAsset> load(const QUuid &id) const;
    std::optional<MediaAsset> findByHash(const QString &hash) const;
    // Every asset, sorted by original name, ignoring case.
    QList<MediaAsset> list() const;
    // The titles of the services with an item showing the asset, so deleting it can ask first.
    // TODO(#99): also count backgrounds set as song and service defaults and as item overrides.
    QStringList usedBy(const QUuid &id) const;
    // Deletes the asset's row, then its file and thumbnail. False if there was no such asset.
    bool remove(const QUuid &id);

    // Absolute paths.
    QString filePath(const MediaAsset &asset) const;
    QString thumbnailPath(const MediaAsset &asset) const;

private:
    Library &m_library;
};

} // namespace visualis
