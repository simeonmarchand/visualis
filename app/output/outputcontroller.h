#pragma once

#include "output/displayplacement.h"
#include "output/displaysettle.h"

#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

Q_DECLARE_LOGGING_CATEGORY(lcOutput)

class QScreen;

// Keeps each output window (audience, stage) borderless on its chosen display. Choices are
// saved by display identity, so they survive restarts, replugs, and the OS renumbering
// displays (plan 5.5). An output is never put on the operator screen; when its display is
// missing it hides until that same display returns.
//
// Display changes wait for the setup to settle before outputs move (DisplaySettle), except
// that an output whose display goes away hides at once, so it can't land on the operator
// screen in the meantime.
class OutputController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Displays an output can go on: every connected display except the operator screen,
    // as {ref, label}. A ref names one connected display, even among identical ones.
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    // Role -> ref of the display the output is on, or "" when it isn't on one.
    Q_PROPERTY(QVariantMap assignments READ assignments NOTIFY placementChanged)
    // Role -> a line for the operator, e.g. "On W156F1" or "Waiting: display not connected".
    Q_PROPERTY(QVariantMap status READ status NOTIFY placementChanged)
    Q_PROPERTY(bool settling READ settling NOTIFY settlingChanged)
    // Names of identical connected displays (same identity, told apart only by position)
    // that the operator hasn't yet confirmed are on the right outputs.
    Q_PROPERTY(QStringList identicalDisplays READ identicalDisplays NOTIFY displaysChanged)

public:
    explicit OutputController(QObject *parent = nullptr);

    Q_INVOKABLE void setOperatorWindow(QQuickWindow *window);
    Q_INVOKABLE void addOutput(const QString &role, QQuickWindow *window);
    // Saves the choice. A role that had this display loses it. Choosing confirms identical displays.
    Q_INVOKABLE void assign(const QString &role, const QString &displayRef);
    // The operator checked that identical displays carry the right outputs; don't ask again.
    Q_INVOKABLE void confirmIdenticalDisplays();

    QVariantList displays() const;
    QVariantMap assignments() const;
    QVariantMap status() const { return m_status; }
    bool settling() const { return m_settle.isSettling(); }
    QStringList identicalDisplays() const;

signals:
    void displaysChanged();
    void placementChanged();
    void settlingChanged();

private:
    QList<visualis::DisplayInfo> currentDisplays() const;
    QScreen *operatorScreen() const;
    void watchScreen(QScreen *screen);
    void onDisplaysChanged(const char *what, QScreen *screen);
    void onScreenRemoved(QScreen *screen);
    void placeAll();
    void place(const QString &role, QQuickWindow *window, const visualis::OutputPlacement &p);
    void setStatus(const QString &role, const QString &status);
    void saveChoices() const;

    visualis::DisplaySettle m_settle;
    QPointer<QQuickWindow> m_operator;
    QMap<QString, QPointer<QQuickWindow>> m_outputs;
    QMap<QString, visualis::DisplayChoice> m_choices;   // saved ones plus this session's defaults
    QMap<QString, visualis::OutputPlacement> m_placements;
    QStringList m_confirmedIdentical;   // display keys
    QStringList m_loggedShared;
    QVariantMap m_status;
};
