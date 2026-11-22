#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace visualis {

// A still image or video in the library's media/ folder (plan 5.2, 5.9). The file is stored
// under its SHA-256, so the same file imported twice is one asset and renaming the original
// breaks nothing.
struct MediaAsset {
    enum class Kind { Image, Video };

    QUuid id;
    QString contentHash;    // SHA-256, lower-case hex
    QString file;           // relative to media/: "ab/abcdef….mp4"
    QString originalName;   // the imported file's name, for the operator
    Kind kind = Kind::Image;
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;  // videos
    double frameRate = 0;   // videos
    QString codec;          // videos: "H.264", "HEVC", or the FourCC
    QDateTime created;
};

QString kindName(MediaAsset::Kind kind);

} // namespace visualis
