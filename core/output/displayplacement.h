#pragma once

#include <QList>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

namespace visualis {

// A connected display as the output code sees it. Plain data, so the placement rules
// below are testable without real screens.
struct DisplayInfo
{
    QString key;        // identity across replugs and restarts (see app/output/platformdisplay.h)
    QString name;       // for showing to the operator
    QRect geometry;
    bool isOperator = false;
};

// The display an output was put on. The position tells apart identical monitors, which
// share a key because they report no serial number (#20).
struct DisplayChoice
{
    QString key;
    QPoint position;

    bool isValid() const { return !key.isEmpty(); }
    bool operator==(const DisplayChoice &) const = default;
};

struct OutputPlacement
{
    enum class Status {
        Placed,
        PlacedInstead,      // the chosen display is the operator screen; on a free one meanwhile
        NoDisplayChosen,    // nothing chosen and no display free
        DisplayMissing,
        OperatorScreen,     // the chosen display is the operator screen and none is free
    };
    Status status = Status::NoDisplayChosen;
    DisplayInfo display;   // set when Placed or PlacedInstead

    bool isPlaced() const { return status == Status::Placed || status == Status::PlacedInstead; }
};

// Decides where every output goes, all at once so two never share a display:
// - An output goes on its chosen display: the one with that key, or among identical ones,
//   the one nearest the saved position. When identical displays are fewer than the outputs
//   that chose them, the nearest choice wins and the others wait.
// - It's never put on the operator screen (plan 5.5). If that's the chosen display, it goes
//   on the next free display instead, left to right, or waits if none is free.
// - When its display is missing, it waits for it rather than moving to another one.
QMap<QString, OutputPlacement> placeOutputs(const QStringList &roles, const QMap<QString, DisplayChoice> &choices,
                                            const QList<DisplayInfo> &displays);

// Gives each role without a choice the next free display, left to right. Free means not
// the operator screen and not where another role's choice puts it. Roles stay unassigned
// when no display is free.
QMap<QString, DisplayChoice> withDefaultDisplays(const QStringList &roles, QMap<QString, DisplayChoice> choices,
                                                 const QList<DisplayInfo> &displays);

// Records `choice` for `role`. Another role that had chosen the same display loses its
// choice, so the two outputs never stack on one display.
QMap<QString, DisplayChoice> withDisplayChosen(QMap<QString, DisplayChoice> choices, const QString &role,
                                               const DisplayChoice &choice, const QList<DisplayInfo> &displays);

// Keys shared by more than one connected display: identical monitors that only their
// position tells apart.
QStringList sharedKeys(const QList<DisplayInfo> &displays);

} // namespace visualis
