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
    // Displays an output can go on: every connected display except the operator screen.
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    Q_PROPERTY(QVariantMap assignments READ assignments NOTIFY placementChanged)
    // Role -> a line for the operator, e.g. "On W156F1" or "Waiting: display not connected".
    Q_PROPERTY(QVariantMap status READ status NOTIFY placementChanged)
    Q_PROPERTY(bool settling READ settling NOTIFY settlingChanged)

public:
    explicit OutputController(QObject *parent = nullptr);

    Q_INVOKABLE void setOperatorWindow(QQuickWindow *window);
    Q_INVOKABLE void addOutput(const QString &role, QQuickWindow *window);
    // Saves the choice. A role that had this display loses it.
    Q_INVOKABLE void assign(const QString &role, const QString &displayKey);

    QVariantList displays() const;
    QVariantMap assignments() const;
    QVariantMap status() const { return m_status; }
    bool settling() const { return m_settle.isSettling(); }

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
    void place(const QString &role, QQuickWindow *window, const QList<visualis::DisplayInfo> &displays);
    void setStatus(const QString &role, const QString &status);

    visualis::DisplaySettle m_settle;
    QPointer<QQuickWindow> m_operator;
    QMap<QString, QPointer<QQuickWindow>> m_outputs;
    QMap<QString, QString> m_assignments;   // role -> display key; saved ones plus this session's defaults
    QVariantMap m_status;
};
