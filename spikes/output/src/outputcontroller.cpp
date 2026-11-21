#include "outputcontroller.h"
#include "eventlog.h"
#include "framestats.h"

#include <QGuiApplication>
#include <QSettings>
#include <QTimer>

OutputController *OutputController::s_instance = nullptr;

OutputController::OutputController(bool windowed, FullscreenMode mode, QObject *parent)
    : QObject(parent)
    , m_windowed(windowed)
    , m_mode(mode)
{
    s_instance = this;

    QSettings settings;
    for (const QString &role : {QStringLiteral("audience"), QStringLiteral("stage")}) {
        const QString key = settings.value("assign/" + role).toString();
        if (!key.isEmpty())
            m_assignments[role] = key;
    }

    auto *app = qGuiApp;
    connect(app, &QGuiApplication::screenAdded, this, [this](QScreen *s) { onScreensChanged("added", s); });
    connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen *s) { onScreensChanged("removed", s); });
    connect(app, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *s) { onScreensChanged("primary changed", s); });
    for (QScreen *s : app->screens()) {
        connect(s, &QScreen::geometryChanged, this, [this, s] { onScreensChanged("geometry changed", s); });
        EventLog::write(QStringLiteral("display present: %1  key=%2  %3x%4 @ %5 Hz")
                            .arg(s->name(), screenKey(s))
                            .arg(s->geometry().width()).arg(s->geometry().height())
                            .arg(s->refreshRate(), 0, 'f', 2));
    }
}

OutputController *OutputController::create(QQmlEngine *, QJSEngine *engine)
{
    Q_ASSERT(s_instance && engine->thread() == s_instance->thread());
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

// Prefer the EDID serial. Without one, fall back to name + manufacturer + model, which
// can't tell two identical serial-less monitors apart; P1 should note if that happens.
// Resolution is left out on purpose so changing it doesn't lose the assignment.
QString OutputController::screenKey(const QScreen *screen)
{
    if (!screen->serialNumber().isEmpty())
        return QStringList{screen->manufacturer(), screen->model(), screen->serialNumber()}.join('|');
    return QStringList{screen->name(), screen->manufacturer(), screen->model()}.join('|');
}

void OutputController::setOperatorWindow(QQuickWindow *window)
{
    m_operator = window;
}

void OutputController::addOutput(const QString &role, QQuickWindow *window)
{
    if (m_windowed) {
        // Dev only: keep outputs above the operator window. macOS stops drawing covered
        // windows, which would spoil the frame measurements.
        window->setFlags(window->flags() | Qt::WindowStaysOnTopHint);
    } else if (m_mode == FullscreenMode::Borderless) {
        window->setFlags(Qt::Window | Qt::FramelessWindowHint);
    }
    m_outputs[role] = window;
    if (!m_assignments.contains(role))
        defaultAssignments();
    place(role, window);
}

QScreen *OutputController::operatorScreen() const
{
    return m_operator && m_operator->screen() ? m_operator->screen() : qGuiApp->primaryScreen();
}

QScreen *OutputController::findScreen(const QString &key) const
{
    for (QScreen *s : qGuiApp->screens()) {
        if (screenKey(s) == key)
            return s;
    }
    return nullptr;
}

QVariantList OutputController::screens() const
{
    QVariantList list;
    const QScreen *op = operatorScreen();
    for (QScreen *s : qGuiApp->screens()) {
        list.append(QVariantMap{
            {"key", screenKey(s)},
            {"label", QStringLiteral("%1 (%2x%3%4)").arg(s->name()).arg(s->geometry().width())
                          .arg(s->geometry().height()).arg(s == op ? ", operator" : "")},
            {"isOperator", s == op},
        });
    }
    return list;
}

void OutputController::defaultAssignments()
{
    QStringList candidates;
    for (QScreen *s : qGuiApp->screens()) {
        if (m_windowed || s != operatorScreen())
            candidates << screenKey(s);
    }
    if (candidates.isEmpty())
        return;
    const QStringList roles{"audience", "stage"};
    for (int i = 0; i < roles.size(); ++i) {
        if (!m_assignments.contains(roles[i]))
            m_assignments[roles[i]] = candidates.value(i, candidates.last());
    }
}

void OutputController::assign(const QString &role, const QString &screenKey)
{
    QScreen *screen = findScreen(screenKey);
    if (screen && !m_windowed && screen == operatorScreen()) {
        EventLog::write(QStringLiteral("refused: %1 can't go on the operator screen").arg(role));
        emit placementChanged();
        return;
    }
    m_assignments[role] = screenKey;
    QSettings().setValue("assign/" + role, screenKey);
    EventLog::write(QStringLiteral("assign %1 -> %2").arg(role, screenKey));
    if (m_outputs.value(role))
        place(role, m_outputs.value(role));
}

void OutputController::onScreensChanged(const QString &what, QScreen *screen)
{
    EventLog::write(QStringLiteral("display %1: %2  key=%3").arg(what, screen->name(), screenKey(screen)));
    if (what == "added")
        connect(screen, &QScreen::geometryChanged, this, [this, screen] { onScreensChanged("geometry changed", screen); });
    // screenRemoved fires while the screen is still listed; place after the change settles.
    QTimer::singleShot(0, this, [this] {
        placeAll();
        emit screensChanged();
    });
}

void OutputController::placeAll()
{
    for (auto it = m_outputs.cbegin(); it != m_outputs.cend(); ++it) {
        if (it.value())
            place(it.key(), it.value());
    }
}

void OutputController::place(const QString &role, QQuickWindow *window)
{
    const QString key = m_assignments.value(role).toString();
    QScreen *screen = findScreen(key);
    QString status;

    if (!screen) {
        window->hide();
        status = "waiting: display not connected";
    } else if (!m_windowed && screen == operatorScreen()) {
        window->hide();
        status = "waiting: assigned display is the operator screen";
    } else if (m_windowed) {
        // Stack the outputs in the right-hand corners, leaving the operator window's controls visible.
        const QRect area = screen->availableGeometry();
        const QSize size(640, 360);
        const int x = area.right() - size.width();
        const int y = role == "audience" ? area.top() : area.bottom() - size.height();
        window->setScreen(screen);
        window->setGeometry(QRect(QPoint(x, y), size));
        window->showNormal();
        status = "windowed on " + screen->name();
    } else if (m_mode == FullscreenMode::Borderless) {
        window->setScreen(screen);
        window->setGeometry(screen->geometry());
        window->show();
        status = "borderless on " + screen->name();
    } else {
        // A fullscreen window has to leave fullscreen before it can move to another display.
        if (window->screen() != screen && window->visibility() == QWindow::FullScreen)
            window->showNormal();
        window->setScreen(screen);
        window->setGeometry(screen->geometry());
        window->showFullScreen();
        status = "fullscreen on " + screen->name();
    }

    FrameStats::instance()->resetTiming(window);
    if (m_placement.value(role).toString() != status) {
        m_placement[role] = status;
        EventLog::write(QStringLiteral("place %1: %2").arg(role, status));
    }
    emit placementChanged();
}
