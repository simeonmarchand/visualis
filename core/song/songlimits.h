#pragma once

#include "song.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

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
// Slide overrides as compact JSON (#95). One with every property set is under 1 KB.
constexpr int kSlideOverrideBytes = 16 * 1024;
constexpr int kOverrideBytes = 1024 * 1024;   // all of a song's together
} // namespace songlimits

// Why the song is over a limit, for the operator ("too many groups: 40,000; the limit is
// 500"), or empty when it's within all of them. Cheap: it never expands the song.
QString songLimitError(const Song &song);

// Slide overrides are bounded on write: SongStore refuses a song for this, and readers drop
// what's over (dropOversizedOverrides) instead, so a song stored before the limits still
// loads, shows, and saves. Empty when within them.
QString overrideLimitError(const Song &song);
bool slideOverrideTooLarge(const QJsonObject &override);
// Empties each slide override over kSlideOverrideBytes, and all of them if the song's are
// still over kOverrideBytes; one warning, for the operator, per drop.
QStringList dropOversizedOverrides(Song &song);

// The same wording for any one limit.
QString limitError(const QString &what, qint64 count, int limit);

} // namespace visualis
