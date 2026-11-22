#include "media/mediaprobe.h"

namespace visualis {

// Visualis plays video through AVFoundation (ADR 0001); elsewhere a video is imported
// unprobed, so it can still be stored and moved to a Mac.
MediaProbe probeVideo(const QString &)
{
    MediaProbe out;
    out.warnings << QStringLiteral("its size, length, and codec weren't read: video probing needs macOS");
    return out;
}

} // namespace visualis
