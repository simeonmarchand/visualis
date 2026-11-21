#include "output/displayplacement.h"

#include <algorithm>

namespace visualis {

OutputPlacement placeOutput(const QString &key, const QList<DisplayInfo> &displays)
{
    if (key.isEmpty())
        return {OutputPlacement::Status::NoDisplayChosen, {}};
    const auto it = std::find_if(displays.cbegin(), displays.cend(),
                                 [&](const DisplayInfo &d) { return d.key == key; });
    if (it == displays.cend())
        return {OutputPlacement::Status::DisplayMissing, {}};
    if (it->isOperator)
        return {OutputPlacement::Status::OperatorScreen, {}};
    return {OutputPlacement::Status::Placed, *it};
}

QMap<QString, QString> withDefaultDisplays(const QStringList &roles, QMap<QString, QString> assignments,
                                           const QList<DisplayInfo> &displays)
{
    QList<DisplayInfo> free;
    for (const DisplayInfo &d : displays) {
        if (!d.isOperator && !assignments.values().contains(d.key))
            free.append(d);
    }
    std::stable_sort(free.begin(), free.end(), [](const DisplayInfo &a, const DisplayInfo &b) {
        return std::pair(a.geometry.x(), a.geometry.y()) < std::pair(b.geometry.x(), b.geometry.y());
    });

    for (const QString &role : roles) {
        if (!assignments.value(role).isEmpty())
            continue;
        if (free.isEmpty())
            break;
        assignments[role] = free.takeFirst().key;
    }
    return assignments;
}

QMap<QString, QString> withDisplayChosen(QMap<QString, QString> assignments, const QString &role,
                                         const QString &key)
{
    for (auto it = assignments.begin(); it != assignments.end();) {
        if (it.key() != role && it.value() == key)
            it = assignments.erase(it);
        else
            ++it;
    }
    assignments[role] = key;
    return assignments;
}

} // namespace visualis
