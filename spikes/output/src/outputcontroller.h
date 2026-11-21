#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QVariantList>
#include <QVariantMap>

// Creates the output windows and keeps each on its assigned display.
// Assignments are saved by display identity (plan 5.5), so they survive restarts,
// re-plugging, and the OS renumbering displays.
class OutputController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(QVariantMap assignments READ assignments NOTIFY placementChanged)
    Q_PROPERTY(QVariantMap placement READ placement NOTIFY placementChanged)
    Q_PROPERTY(bool windowed READ windowed CONSTANT)

public:
    enum class FullscreenMode { Native, Borderless };

    OutputController(bool windowed, FullscreenMode mode, QObject *parent = nullptr);

    static OutputController *instance() { return s_instance; }
    static OutputController *create(QQmlEngine *, QJSEngine *engine);

    static QString screenKey(const QScreen *screen);

    void addOutput(const QString &role, QQuickWindow *window);
    void setOperatorWindow(QQuickWindow *window);

    QVariantList screens() const;
    QVariantMap assignments() const { return m_assignments; }
    QVariantMap placement() const { return m_placement; }
    bool windowed() const { return m_windowed; }

    Q_INVOKABLE void assign(const QString &role, const QString &screenKey);

signals:
    void screensChanged();
    void placementChanged();

private:
    QScreen *operatorScreen() const;
    QScreen *findScreen(const QString &key) const;
    void defaultAssignments();
    void placeAll();
    void place(const QString &role, QQuickWindow *window);
    void onScreensChanged(const QString &what, QScreen *screen);

    static OutputController *s_instance;
    bool m_windowed;
    FullscreenMode m_mode;
    QPointer<QQuickWindow> m_operator;
    QMap<QString, QPointer<QQuickWindow>> m_outputs;
    QVariantMap m_assignments; // role -> screen key
    QVariantMap m_placement;   // role -> human-readable status
};
