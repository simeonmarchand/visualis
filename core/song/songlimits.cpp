#include "songlimits.h"

#include <QJsonDocument>
#include <QLocale>

namespace visualis {

using namespace songlimits;

namespace {
QString number(qint64 n) { return QLocale(QLocale::English).toString(n); }
} // namespace

QString limitError(const QString &what, qint64 count, int limit)
{
    return QStringLiteral("too many %1: %2; the limit is %3").arg(what, number(count), number(limit));
}

QString songLimitError(const Song &song)
{
    if (song.groups.size() > kGroups)
        return limitError(QStringLiteral("groups"), song.groups.size(), kGroups);
    qint64 slides = 0;
    for (const Group &g : song.groups) {
        if (g.slides.size() > kSlidesPerGroup)
            return limitError(QStringLiteral("slides in \"%1\"").arg(g.label), g.slides.size(), kSlidesPerGroup);
        slides += g.slides.size();
        for (const QString &s : g.slides) {
            if (s.size() > kCharactersPerSlide)
                return limitError(QStringLiteral("characters in a slide of \"%1\"").arg(g.label), s.size(), kCharactersPerSlide);
        }
    }
    if (slides > kSlides)
        return limitError(QStringLiteral("slides"), slides, kSlides);
    if (song.arrangements.size() > kArrangements)
        return limitError(QStringLiteral("arrangements"), song.arrangements.size(), kArrangements);
    for (const Arrangement &a : song.arrangements) {
        if (a.sequence.size() > kSteps)
            return limitError(QStringLiteral("steps in arrangement \"%1\"").arg(a.name), a.sequence.size(), kSteps);
    }
    return {};
}

namespace {
qint64 overrideBytes(const QJsonObject &o)
{
    return o.isEmpty() ? 0 : QJsonDocument(o).toJson(QJsonDocument::Compact).size();
}
} // namespace

bool slideOverrideTooLarge(const QJsonObject &o)
{
    return overrideBytes(o) > kSlideOverrideBytes;
}

QString overrideLimitError(const Song &song)
{
    qint64 total = 0;
    for (const Group &g : song.groups) {
        for (const QJsonObject &o : g.slideOverrides) {
            const qint64 bytes = overrideBytes(o);
            if (bytes > kSlideOverrideBytes)
                return QStringLiteral("a slide of \"%1\" has too much style: %2 bytes; the limit is %3")
                    .arg(g.label, number(bytes), number(kSlideOverrideBytes));
            total += bytes;
        }
    }
    if (total > kOverrideBytes)
        return QStringLiteral("too much slide style: %1 bytes; the limit is %2").arg(number(total), number(kOverrideBytes));
    return {};
}

QStringList dropOversizedOverrides(Song &song)
{
    QStringList warnings;
    qint64 total = 0;
    for (Group &g : song.groups) {
        for (qsizetype i = 0; i < g.slideOverrides.size(); ++i) {
            if (slideOverrideTooLarge(g.slideOverrides[i])) {
                warnings << QStringLiteral("slide %1 of \"%2\" has too much style (over %3 KB), so its style changes were left out")
                                .arg(i + 1).arg(g.label).arg(kSlideOverrideBytes / 1024);
                g.slideOverrides[i] = {};
            }
            total += overrideBytes(g.slideOverrides[i]);
        }
    }
    if (total > kOverrideBytes) {
        warnings << QStringLiteral("\"%1\" has too much slide style (over %2 KB), so every slide's style changes were left out")
                        .arg(song.title).arg(kOverrideBytes / 1024);
        for (Group &g : song.groups)
            g.slideOverrides.fill({});
    }
    return warnings;
}

} // namespace visualis
