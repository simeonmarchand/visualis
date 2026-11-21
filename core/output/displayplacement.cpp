#include "output/displayplacement.h"

#include <QSet>

#include <algorithm>

namespace visualis {

namespace {

// Indexes of displays in `candidates`, left to right.
QList<int> leftToRight(const QList<DisplayInfo> &displays, QList<int> candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        const QPoint pa = displays[a].geometry.topLeft(), pb = displays[b].geometry.topLeft();
        return std::pair(pa.x(), pa.y()) < std::pair(pb.x(), pb.y());
    });
    return candidates;
}

} // namespace

QMap<QString, OutputPlacement> placeOutputs(const QStringList &roles, const QMap<QString, DisplayChoice> &choices,
                                            const QList<DisplayInfo> &displays)
{
    // Match choices to displays nearest-first, so among identical displays each output gets
    // the one closest to where it was, and the closest claim wins when there are too few.
    struct Candidate { int role; int display; int distance; };
    QList<Candidate> candidates;
    for (int r = 0; r < roles.size(); ++r) {
        const DisplayChoice choice = choices.value(roles[r]);
        for (int d = 0; d < displays.size(); ++d) {
            if (choice.isValid() && displays[d].key == choice.key)
                candidates.append({r, d, (displays[d].geometry.topLeft() - choice.position).manhattanLength()});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) { return a.distance < b.distance; });

    QMap<int, int> matched;   // role -> display
    QSet<int> taken;
    for (const Candidate &c : candidates) {
        if (matched.contains(c.role) || taken.contains(c.display))
            continue;
        matched[c.role] = c.display;
        taken.insert(c.display);
    }

    QMap<QString, OutputPlacement> result;
    QList<int> onOperatorScreen;
    for (int r = 0; r < roles.size(); ++r) {
        OutputPlacement &p = result[roles[r]];
        if (!choices.value(roles[r]).isValid())
            p.status = OutputPlacement::Status::NoDisplayChosen;
        else if (!matched.contains(r))
            p.status = OutputPlacement::Status::DisplayMissing;
        else if (displays[matched[r]].isOperator)
            onOperatorScreen.append(r);
        else
            p = {OutputPlacement::Status::Placed, displays[matched[r]]};
    }

    QList<int> free;
    for (int d = 0; d < displays.size(); ++d) {
        if (!displays[d].isOperator && !taken.contains(d))
            free.append(d);
    }
    free = leftToRight(displays, free);
    for (int r : onOperatorScreen) {
        if (free.isEmpty())
            result[roles[r]].status = OutputPlacement::Status::OperatorScreen;
        else
            result[roles[r]] = {OutputPlacement::Status::PlacedInstead, displays[free.takeFirst()]};
    }
    return result;
}

QMap<QString, DisplayChoice> withDefaultDisplays(const QStringList &roles, QMap<QString, DisplayChoice> choices,
                                                 const QList<DisplayInfo> &displays)
{
    const QMap<QString, OutputPlacement> placed = placeOutputs(roles, choices, displays);
    QList<int> free;
    for (int d = 0; d < displays.size(); ++d) {
        const bool occupied = std::any_of(placed.cbegin(), placed.cend(), [&](const OutputPlacement &p) {
            return p.isPlaced() && p.display.key == displays[d].key && p.display.geometry == displays[d].geometry;
        });
        if (!displays[d].isOperator && !occupied)
            free.append(d);
    }
    free = leftToRight(displays, free);

    for (const QString &role : roles) {
        if (choices.value(role).isValid())
            continue;
        if (free.isEmpty())
            break;
        const DisplayInfo &d = displays[free.takeFirst()];
        choices[role] = {d.key, d.geometry.topLeft()};
    }
    return choices;
}

QMap<QString, DisplayChoice> withDisplayChosen(QMap<QString, DisplayChoice> choices, const QString &role,
                                               const DisplayChoice &choice, const QList<DisplayInfo> &displays)
{
    const QMap<QString, OutputPlacement> placed = placeOutputs(choices.keys(), choices, displays);
    for (auto it = choices.begin(); it != choices.end();) {
        const OutputPlacement p = placed.value(it.key());
        // Same display: the same saved choice, or (after displays were rearranged) one that
        // currently resolves to the display being chosen.
        const bool same = it.value() == choice
            || (p.status == OutputPlacement::Status::Placed && p.display.key == choice.key
                && p.display.geometry.topLeft() == choice.position);
        if (it.key() != role && same)
            it = choices.erase(it);
        else
            ++it;
    }
    choices[role] = choice;
    return choices;
}

QStringList sharedKeys(const QList<DisplayInfo> &displays)
{
    QMap<QString, int> counts;
    for (const DisplayInfo &d : displays)
        ++counts[d.key];
    QStringList shared;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > 1)
            shared.append(it.key());
    }
    return shared;
}

} // namespace visualis
