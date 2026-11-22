#include "slidelist.h"

#include "labels.h"

namespace visualis {

QList<SongSlide> songSlides(const Song &song, const QUuid &arrangement)
{
    QList<SongSlide> out;
    const QList<QUuid> order = song.playOrder(arrangement);
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
    static const QStringList kinds{"intro", "outro", "ending", "interlude", "instrumental", "turnaround"};
    const SectionLabel parsed = parseLabel(label);
    return parsed.known && kinds.contains(parsed.kind);
}

QList<SongSlide> songSlides(const Song &song, const QStringList &sequence)
{
    QList<SongSlide> out;
    const SequenceMatch match = matchSequence(sequence, song.groups);
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
