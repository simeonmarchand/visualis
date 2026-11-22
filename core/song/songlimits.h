#pragma once

#include "song.h"

#include <QString>

namespace visualis {

// How big a song may be, so one huge file (an import, a crash journal) can't make the GUI
// thread save tens of thousands of rows or expand millions of slides (#83). A false refusal
// on a Sunday is worse than a slow import, so each is at least 18x the largest in the
// church's ProPresenter library (151 songs, measured 2026-11: 24 groups, 15 slides in a
// group, 41 slides, 2 arrangements, 36 steps, 555 characters in a slide, 71 played).
namespace songlimits {
constexpr int kGroups = 500;
constexpr int kSlidesPerGroup = 500;
constexpr int kSlides = 10'000;           // in all groups together
constexpr int kArrangements = 100;
constexpr int kSteps = 1'000;             // per arrangement, or a played sequence
constexpr int kCharactersPerSlide = 10'000;
constexpr int kPlayedSlides = 10'000;     // one arrangement expanded (songSlides)
} // namespace songlimits

// Why the song is over a limit, for the operator ("too many groups: 40,000; the limit is
// 500"), or empty when it's within all of them. Cheap: it never expands the song.
QString songLimitError(const Song &song);

// The same wording for any one limit.
QString limitError(const QString &what, qint64 count, int limit);

} // namespace visualis
