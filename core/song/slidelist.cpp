#include "slidelist.h"

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

} // namespace visualis
