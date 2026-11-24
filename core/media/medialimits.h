#pragma once

#include <QtGlobal>

namespace visualis {

// How big imported media may be, so one bad file can't fill the disk or make a worker decode
// a gigapixel image (#97). Generous, as songlimits.h: a refused background on a Sunday is
// worse than a slow import. Motion backgrounds are typically 50 to 500 MB; a long 4K ProRes
// clip can be several GB.
namespace medialimits {
constexpr qint64 kImageBytes = 200LL * 1024 * 1024;
constexpr qint64 kVideoBytes = 20LL * 1024 * 1024 * 1024;
constexpr int kImageAllocationMb = 1024;   // QImageReader's decode limit: about 16k x 16k RGBA
constexpr int kThumbnailPixels = 320;      // the longer side
} // namespace medialimits

} // namespace visualis
