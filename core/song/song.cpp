#include "song.h"

#include <algorithm>

namespace visualis {

bool Group::operator==(const Group &other) const
{
    if (id != other.id || label != other.label || color != other.color || slides != other.slides)
        return false;
    for (qsizetype i = 0; i < slides.size(); ++i) {
        if (slideOverride(i) != other.slideOverride(i))
            return false;
    }
    return true;
}

const Group *Song::group(const QUuid &id) const
{
    auto it = std::find_if(groups.cbegin(), groups.cend(), [&](const Group &g) { return g.id == id; });
    return it == groups.cend() ? nullptr : &*it;
}

Group *Song::group(const QUuid &id)
{
    return const_cast<Group *>(std::as_const(*this).group(id));
}

const Arrangement *Song::arrangement(const QUuid &id) const
{
    auto it = std::find_if(arrangements.cbegin(), arrangements.cend(), [&](const Arrangement &a) { return a.id == id; });
    return it == arrangements.cend() ? nullptr : &*it;
}

void Song::removeGroup(const QUuid &id)
{
    groups.removeIf([&](const Group &g) { return g.id == id; });
    for (Arrangement &a : arrangements)
        a.sequence.removeAll(id);
}

QList<QUuid> Song::playOrder(const QUuid &arrangementId) const
{
    const Arrangement *a = nullptr;
    if (!arrangementId.isNull())
        a = arrangement(arrangementId);
    if (!a && !defaultArrangement.isNull())
        a = arrangement(defaultArrangement);
    if (!a && !arrangements.isEmpty())
        a = &arrangements.first();
    if (a)
        return a->sequence;

    QList<QUuid> all;
    for (const Group &g : groups)
        all << g.id;
    return all;
}

} // namespace visualis
