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

QString refOf(const DisplayInfo &d)
{
    return QStringLiteral("%1@%2,%3").arg(d.key).arg(d.geometry.x()).arg(d.geometry.y());
}

QString sizeLabel(const DisplayInfo &d)
{
    return QStringLiteral("%1 (%2x%3)").arg(d.name).arg(d.geometry.width()).arg(d.geometry.height());
}

// Identical displays get their position in the name: "W156F1 (1920x1080), left".
QString labelOf(const DisplayInfo &d, const QList<DisplayInfo> &displays)
{
    QList<int> xs;
    for (const DisplayInfo &other : displays) {
        if (other.key == d.key)
            xs.append(other.geometry.x());
    }
    if (xs.size() < 2)
        return sizeLabel(d);
    std::sort(xs.begin(), xs.end());
    const int n = int(xs.indexOf(d.geometry.x()));
    if (xs.size() == 2)
        return sizeLabel(d) + (n == 0 ? ", left" : ", right");
    return sizeLabel(d) + QStringLiteral(", %1 of %2 from left").arg(n + 1).arg(xs.size());
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
            m_choices[role] = {key, settings.value("outputs/" + role + "-position").toPoint()};
    }
    m_confirmedIdentical = settings.value("outputs/confirmed-identical").toStringList();

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

void OutputController::assign(const QString &role, const QString &ref)
{
    const QList<DisplayInfo> displays = currentDisplays();
    const auto it = std::find_if(displays.cbegin(), displays.cend(), [&](const DisplayInfo &d) { return refOf(d) == ref; });
    if (it == displays.cend()) {
        qCWarning(lcOutput).noquote() << "assign" << role << ": no display" << ref;   // unplugged meanwhile
        emit placementChanged();
        return;
    }
    m_choices = withDisplayChosen(m_choices, role, {it->key, it->geometry.topLeft()}, displays);
    saveChoices();
    qCInfo(lcOutput).noquote() << "assign" << role << "->" << ref;
    // Picking a display while identical ones are connected is the operator telling them apart.
    if (!identicalDisplays().isEmpty())
        confirmIdenticalDisplays();
    if (m_settle.isSettling())
        emit placementChanged();   // placed once the displays settle
    else
        placeAll();
}

void OutputController::confirmIdenticalDisplays()
{
    for (const QString &key : sharedKeys(currentDisplays())) {
        if (!m_confirmedIdentical.contains(key))
            m_confirmedIdentical.append(key);
    }
    QSettings().setValue("outputs/confirmed-identical", m_confirmedIdentical);
    emit displaysChanged();
}

QStringList OutputController::identicalDisplays() const
{
    const QList<DisplayInfo> displays = currentDisplays();
    QStringList names;
    for (const QString &key : sharedKeys(displays)) {
        if (m_confirmedIdentical.contains(key))
            continue;
        for (const DisplayInfo &d : displays) {
            if (d.key == key) {
                names.append(d.name);
                break;
            }
        }
    }
    return names;
}

void OutputController::saveChoices() const
{
    // Only explicit choices are saved; defaults are recomputed each session.
    QSettings settings;
    for (const QString &r : kRoles) {
        if (m_choices.contains(r)) {
            settings.setValue("outputs/" + r, m_choices.value(r).key);
            settings.setValue("outputs/" + r + "-position", m_choices.value(r).position);
        } else {
            settings.remove("outputs/" + r);
            settings.remove("outputs/" + r + "-position");
        }
    }
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
    const QList<DisplayInfo> all = currentDisplays();
    QVariantList list;
    for (const DisplayInfo &d : all) {
        if (!d.isOperator)
            list.append(QVariantMap{{"ref", refOf(d)}, {"label", labelOf(d, all)}});
    }
    return list;
}

QVariantMap OutputController::assignments() const
{
    QVariantMap map;
    for (const QString &role : kRoles) {
        const OutputPlacement p = m_placements.value(role);
        map[role] = p.status == OutputPlacement::Status::Placed ? refOf(p.display) : QString();
    }
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
    m_choices = withDefaultDisplays(kRoles, m_choices, displays);
    m_placements = placeOutputs(kRoles, m_choices, displays);
    if (const QStringList shared = sharedKeys(displays); shared != m_loggedShared) {
        m_loggedShared = shared;
        if (!shared.isEmpty())
            qCInfo(lcOutput).noquote() << "identical displays:" << identicalDisplays().join(", ");
    }
    for (const QString &role : kRoles) {
        if (QQuickWindow *window = m_outputs.value(role))
            place(role, window, m_placements.value(role));
    }
    emit placementChanged();
}

void OutputController::place(const QString &role, QQuickWindow *window, const OutputPlacement &p)
{
    switch (p.status) {
    case OutputPlacement::Status::Placed:
    case OutputPlacement::Status::PlacedInstead:
        break;
    case OutputPlacement::Status::NoDisplayChosen:
        window->hide();
        setStatus(role, QStringLiteral("Waiting: no free display"));
        return;
    case OutputPlacement::Status::DisplayMissing:
        window->hide();
        setStatus(role, QStringLiteral("Waiting: display not connected"));
        return;
    case OutputPlacement::Status::OperatorScreen:
        window->hide();
        setStatus(role, QStringLiteral("Waiting: chosen display is the operator screen, and no other is free"));
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
    setStatus(role, p.status == OutputPlacement::Status::Placed
                        ? QStringLiteral("On %1").arg(p.display.name)
                        : QStringLiteral("On %1 for now: the chosen display is the operator screen").arg(p.display.name));
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
