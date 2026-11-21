#include "output/outputcontroller.h"
#include "output/platformdisplay.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

using namespace visualis;
using namespace std::chrono_literals;

Q_LOGGING_CATEGORY(lcOutput, "visualis.output")

namespace {

const QStringList kRoles{"audience", "stage"};

// About 2 s with no display change: long enough to ride out a monitor that reconnects
// every second after wake (#4), short enough that a replug feels prompt.
constexpr auto kSettleTime = 2s;

QString describe(const QScreen *s)
{
    return QStringLiteral("%1 (%2x%3)").arg(s->name()).arg(s->geometry().width()).arg(s->geometry().height());
}

} // namespace

OutputController::OutputController(QObject *parent)
    : QObject(parent)
    , m_settle(kSettleTime)
{
    QSettings settings;
    for (const QString &role : kRoles) {
        const QString key = settings.value("outputs/" + role).toString();
        if (!key.isEmpty())
            m_assignments[role] = key;
    }

    connect(&m_settle, &DisplaySettle::settlingChanged, this, [this](bool settling) {
        qCInfo(lcOutput) << (settling ? "displays settling" : "displays settled");
        emit settlingChanged();
    });
    connect(&m_settle, &DisplaySettle::settled, this, [this] {
        placeAll();
        emit displaysChanged();
    });

    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *s) {
        watchScreen(s);
        // The name arrives a moment later with a geometry change, so this isn't final.
        onDisplaysChanged("added", s);
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OutputController::onScreenRemoved);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this,
            [this](QScreen *s) { onDisplaysChanged("primary changed", s); });
    for (QScreen *s : QGuiApplication::screens()) {
        watchScreen(s);
        qCInfo(lcOutput).noquote() << "display present:" << describe(s) << "key" << displayKey(s);
    }
}

void OutputController::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, [this, screen] { onDisplaysChanged("changed", screen); });
}

void OutputController::setOperatorWindow(QQuickWindow *window)
{
    m_operator = window;
    // If the operator moves their window onto an output's display, that output must get out
    // of the way now, not after a settle.
    connect(window, &QWindow::screenChanged, this, [this] {
        if (!m_settle.isSettling())
            placeAll();
        emit displaysChanged();
    });
}

void OutputController::addOutput(const QString &role, QQuickWindow *window)
{
    m_outputs[role] = window;
    if (!m_settle.isSettling())
        placeAll();
}

void OutputController::assign(const QString &role, const QString &key)
{
    m_assignments = withDisplayChosen(m_assignments, role, key);
    QSettings settings;
    for (const QString &r : kRoles) {
        if (m_assignments.contains(r))
            settings.setValue("outputs/" + r, m_assignments.value(r));
        else
            settings.remove("outputs/" + r);
    }
    qCInfo(lcOutput).noquote() << "assign" << role << "->" << key;
    if (m_settle.isSettling())
        emit placementChanged();   // placed once the displays settle
    else
        placeAll();
}

QScreen *OutputController::operatorScreen() const
{
    return m_operator && m_operator->screen() ? m_operator->screen() : QGuiApplication::primaryScreen();
}

QList<DisplayInfo> OutputController::currentDisplays() const
{
    const QScreen *op = operatorScreen();
    QList<DisplayInfo> list;
    for (QScreen *s : QGuiApplication::screens())
        list.append(DisplayInfo{displayKey(s), s->name(), s->geometry(), s == op});
    return list;
}

QVariantList OutputController::displays() const
{
    QVariantList list;
    for (const DisplayInfo &d : currentDisplays()) {
        if (d.isOperator)
            continue;
        list.append(QVariantMap{
            {"key", d.key},
            {"label", QStringLiteral("%1 (%2x%3)").arg(d.name).arg(d.geometry.width()).arg(d.geometry.height())},
        });
    }
    return list;
}

QVariantMap OutputController::assignments() const
{
    QVariantMap map;
    for (auto it = m_assignments.cbegin(); it != m_assignments.cend(); ++it)
        map[it.key()] = it.value();
    return map;
}

void OutputController::onDisplaysChanged(const char *what, QScreen *screen)
{
    qCInfo(lcOutput).noquote() << "display" << what << describe(screen) << "key" << displayKey(screen);
    m_settle.changed();
}

void OutputController::onScreenRemoved(QScreen *screen)
{
    // Qt moves a removed display's windows to another display, possibly the operator's.
    // Hide them first; where they go next is decided once the displays settle.
    for (auto it = m_outputs.cbegin(); it != m_outputs.cend(); ++it) {
        QQuickWindow *window = it.value();
        if (window && window->isVisible() && window->screen() == screen) {
            window->hide();
            setStatus(it.key(), QStringLiteral("Waiting: display not connected"));
        }
    }
    emit placementChanged();
    onDisplaysChanged("removed", screen);
}

void OutputController::placeAll()
{
    const QList<DisplayInfo> displays = currentDisplays();
    // Defaults aren't saved: until the operator chooses, an unassigned output takes the next
    // free display each time, including one plugged in later.
    m_assignments = withDefaultDisplays(kRoles, m_assignments, displays);
    for (const QString &role : kRoles) {
        if (QQuickWindow *window = m_outputs.value(role))
            place(role, window, displays);
    }
    emit placementChanged();
}

void OutputController::place(const QString &role, QQuickWindow *window, const QList<DisplayInfo> &displays)
{
    const OutputPlacement p = placeOutput(m_assignments.value(role), displays);
    if (p.status != OutputPlacement::Status::Placed) {
        window->hide();
        switch (p.status) {
        case OutputPlacement::Status::NoDisplayChosen:
            setStatus(role, QStringLiteral("Waiting: no free display"));
            break;
        case OutputPlacement::Status::DisplayMissing:
            setStatus(role, QStringLiteral("Waiting: display not connected"));
            break;
        case OutputPlacement::Status::OperatorScreen:
            setStatus(role, QStringLiteral("Waiting: chosen display is the operator screen"));
            break;
        case OutputPlacement::Status::Placed:
            break;
        }
        return;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    const auto it = std::find_if(screens.cbegin(), screens.cend(),
                                 [&](QScreen *s) { return s->geometry() == p.display.geometry; });
    if (it == screens.cend())
        return;
    const bool moved = !window->isVisible() || window->screen() != *it || window->geometry() != (*it)->geometry();
    window->setScreen(*it);
    window->setGeometry((*it)->geometry());
    window->show();
    raiseAboveMenuBar(window);
    setStatus(role, QStringLiteral("On %1").arg(p.display.name));
    if (moved)
        qCInfo(lcOutput).noquote() << role << "placed on" << describe(*it);
}

void OutputController::setStatus(const QString &role, const QString &status)
{
    if (m_status.value(role).toString() == status)
        return;
    m_status[role] = status;
    qCInfo(lcOutput).noquote() << role + ":" << status;
}
