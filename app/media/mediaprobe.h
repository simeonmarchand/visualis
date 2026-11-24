#pragma once

#include "media/mediaasset.h"

#include <QImage>
#include <QString>
#include <QStringList>

namespace visualis {

// What a media file turned out to be, read on a worker thread during an import (#97): its
// size and timing for the library, and a small first-frame image for the media pane.
struct MediaProbe {
    QString error;          // why it can't be shown, for the operator; empty when it can
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;  // videos
    double frameRate = 0;   // videos
    QString codec;          // videos
    QImage thumbnail;       // at most medialimits::kThumbnailPixels on the longer side
    QStringList warnings;   // e.g. a codec this Mac can't decode in hardware (plan 5.6)
    bool ok() const { return error.isEmpty(); }
};

// Reads the file without loading it whole: images through QImageReader with a decode limit,
// videos through the platform's media framework. Safe on any thread; never touches a library.
MediaProbe probeMedia(const QString &path, MediaAsset::Kind kind);

// The video half, per platform (mediaprobe_mac.mm, mediaprobe_other.cpp).
MediaProbe probeVideo(const QString &path);

// A readable name for a video codec's four-character code: "H.264", "HEVC", "ProRes 422".
QString codecName(quint32 fourCC);

} // namespace visualis
