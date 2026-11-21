#pragma once

#include <QList>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>

namespace visualis {

// A connected display as the output code sees it. Plain data, so the placement rules
// below are testable without real screens.
struct DisplayInfo
{
    QString key;        // stable identity across replugs and restarts (see app/output/platformdisplay.h)
    QString name;       // for showing to the operator
    QRect geometry;
    bool isOperator = false;
};

struct OutputPlacement
{
    enum class Status { Placed, NoDisplayChosen, DisplayMissing, OperatorScreen };
    Status status = Status::NoDisplayChosen;
    DisplayInfo display;   // set when Placed
};

// Where an output assigned to the display `key` goes. It is never placed on the operator
// screen (plan 5.5), and when its display is missing it waits for that same display rather
// than moving to another one.
OutputPlacement placeOutput(const QString &key, const QList<DisplayInfo> &displays);

// Gives each role without a display the next free one, left to right. Free means not the
// operator screen and not already chosen for another role, so two outputs never share a
// display. Roles stay unassigned when no display is free.
QMap<QString, QString> withDefaultDisplays(const QStringList &roles, QMap<QString, QString> assignments,
                                           const QList<DisplayInfo> &displays);

// Assigns `key` to `role`. Another role that had that display loses it, so the two
// outputs never stack on one display.
QMap<QString, QString> withDisplayChosen(QMap<QString, QString> assignments, const QString &role,
                                         const QString &key);

} // namespace visualis
