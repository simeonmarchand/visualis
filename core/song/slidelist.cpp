#include "slidelist.h"

#include "labels.h"
#include "songlimits.h"

namespace visualis {

namespace {

// A group plays at least one slide, even with none of its own.
qint64 played(const Group &g)
{
    return qMax<qint64>(1, g.slides.size());
}

bool refuse(const QString &why, QString *error)
{
    if (error)
        *error = why;
    return true;
}

// Whether to refuse before expanding: within the structural limits, the steps are bounded,
// so counting is cheap.
bool overLimits(const Song &song, QString *error)
{
    const QString e = songLimitError(song);
    return !e.isEmpty() && refuse(e, error);
}

bool tooManyPlayed(qint64 count, QString *error)
{
    return count > songlimits::kPlayedSlides
        && refuse(limitError(QStringLiteral("slides to play"), count, songlimits::kPlayedSlides), error);
}

} // namespace

QList<SongSlide> songSlides(const Song &song, const QUuid &arrangement, QString *error)
{
    QList<SongSlide> out;
    if (overLimits(song, error))
        return out;
    const QList<QUuid> order = song.playOrder(arrangement);
    qint64 count = 0;
    for (const QUuid &id : order) {
        if (const Group *g = song.group(id))
            count += played(*g);
    }
    if (tooManyPlayed(count, error))
        return out;
    out.reserve(count);
    for (int step = 0; step < order.size(); ++step) {
        const Group *g = song.group(order[step]);
        if (!g)
            continue;
        const QStringList slides = g->slides.isEmpty() ? QStringList{QString()} : g->slides;
        for (int i = 0; i < slides.size(); ++i)
            out << SongSlide{slides[i], g->id, g->label, g->color, step, i, int(slides.size())};
    }
    return out;
}

bool isLyricless(const QString &label)
{
    static const QStringList kinds{"intro", "outro", "ending", "interlude", "instrumental", "turnaround", "blank"};
    const SectionLabel parsed = parseLabel(label);
    return parsed.known && kinds.contains(parsed.kind);
}

QList<SongSlide> songSlides(const Song &song, const QStringList &sequence, QString *error)
{
    QList<SongSlide> out;
    if (overLimits(song, error))
        return out;
    if (sequence.size() > songlimits::kSteps) {
        refuse(limitError(QStringLiteral("steps in the sequence"), sequence.size(), songlimits::kSteps), error);
        return out;
    }
    const SequenceMatch match = matchSequence(sequence, song.groups);
    qint64 count = 0;
    for (int step = 0; step < sequence.size(); ++step) {
        const Group *g = match.steps[step].isNull() ? nullptr : song.group(match.steps[step]);
        count += g ? played(*g) : 1;
    }
    if (tooManyPlayed(count, error))
        return out;
    for (int step = 0; step < sequence.size(); ++step) {
        const Group *g = match.steps[step].isNull() ? nullptr : song.group(match.steps[step]);
        if (!g) {
            if (isLyricless(sequence[step]))
                out << SongSlide{QString(), QUuid(), sequence[step], QString(), step, 0, 1};
            continue;
        }
        const QStringList slides = g->slides.isEmpty() ? QStringList{QString()} : g->slides;
        for (int i = 0; i < slides.size(); ++i)
            out << SongSlide{slides[i], g->id, g->label, g->color, step, i, int(slides.size())};
    }
    return out;
}

} // namespace visualis
