#include "scriptrunner.h"
#include "eventlog.h"
#include "framestats.h"
#include "livestate.h"
#include "outputcontroller.h"
#include "videoengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

ScriptRunner::ScriptRunner(const QString &scriptPath, const QString &outDir,
                           const QMap<QString, QQuickWindow *> &windows, QObject *parent)
    : QObject(parent)
    , m_outDir(outDir)
{
    for (auto it = windows.cbegin(); it != windows.cend(); ++it)
        m_windows[it.key()] = it.value();

    QFile file(scriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("cannot read script %s", qPrintable(scriptPath));
    int number = 0;
    while (!file.atEnd()) {
        ++number;
        const QString text = QString::fromUtf8(file.readLine()).trimmed();
        if (text.isEmpty() || text.startsWith('#'))
            continue;
        m_lines.append({number, text.split(' ', Qt::SkipEmptyParts)});
    }
    QDir().mkpath(m_outDir + "/snapshots");
    EventLog::write(QStringLiteral("script %1: %2 steps").arg(scriptPath).arg(m_lines.size()));
}

void ScriptRunner::start()
{
    QTimer::singleShot(0, this, &ScriptRunner::step);
}

void ScriptRunner::step()
{
    if (m_pos >= m_lines.size()) {
        finish();
        return;
    }
    const int delay = run(m_lines[m_pos]);
    if (delay < 0) {
        QTimer::singleShot(100, this, &ScriptRunner::step); // still waiting: poll the same line
        return;
    }
    ++m_pos;
    QTimer::singleShot(delay, this, &ScriptRunner::step);
}

void ScriptRunner::finish()
{
    EventLog::write(QStringLiteral("script done: %1 checks, %2 failed, %3 snapshots in %4/snapshots")
                        .arg(m_checks).arg(m_failures).arg(m_snapshots).arg(m_outDir));
    QCoreApplication::exit(m_failures ? 1 : 0);
}

void ScriptRunner::fail(const Line &line, const QString &why)
{
    ++m_failures;
    EventLog::write(QStringLiteral("FAIL line %1 (%2): %3").arg(line.number).arg(line.words.join(' '), why));
}

int ScriptRunner::run(const Line &line)
{
    const QStringList &w = line.words;
    const QString cmd = w.first();
    const QString rest = w.mid(1).join(' ');
    auto *live = LiveState::instance();
    auto *video = VideoEngine::instance();

    if (cmd == "next") { live->next(); return 0; }
    if (cmd == "prev") { live->prev(); return 0; }
    if (cmd == "goto") { live->goTo(rest.toInt() - 1); return 0; }
    if (cmd == "clear-text") { live->clearText(); return 0; }
    if (cmd == "background") { video->setBackgroundVisible(rest == "on"); return 0; }
    if (cmd == "clear-all") {   // same as the operator window's Clear all
        live->clearText();
        video->setBackgroundVisible(false);
        return 0;
    }
    if (cmd == "mode") { video->setMode(rest); return 0; }
    if (cmd == "video") { video->setSource(QUrl::fromLocalFile(QFileInfo(rest).absoluteFilePath())); return 0; }
    if (cmd == "assign") {
        const int space = rest.indexOf(' ');
        OutputController::instance()->assign(rest.left(space), rest.mid(space + 1));
        return 0;
    }
    if (cmd == "wait") return rest.toInt();
    if (cmd == "snapshot") { snapshot(rest); return 0; }

    if (cmd == "expect" && w.size() >= 4) {
        ++m_checks;
        QString detail;
        if (check(w.mid(1), &detail))
            EventLog::write(QStringLiteral("ok   line %1 (%2)").arg(line.number).arg(w.join(' ')));
        else
            fail(line, detail);
        return 0;
    }

    if (cmd == "wait-for" && w.size() >= 4) {
        // The timeout is the last word only when it's a number, so values may contain spaces.
        bool hasTimeout = false;
        const int lastNumber = w.last().toInt(&hasTimeout);
        hasTimeout = hasTimeout && w.size() >= 5;
        const QStringList cond = w.mid(1, w.size() - (hasTimeout ? 2 : 1));
        const int timeoutMs = hasTimeout ? lastNumber : 5000;
        if (!m_waiting) {
            m_waiting = true;
            m_waitingSince.start();
        }
        QString detail;
        if (check(cond, &detail)) {
            ++m_checks;
            m_waiting = false;
            EventLog::write(QStringLiteral("ok   line %1 (%2) after %3 ms")
                                .arg(line.number).arg(w.join(' ')).arg(m_waitingSince.elapsed()));
            return 0;
        }
        if (m_waitingSince.elapsed() < timeoutMs)
            return -1;
        ++m_checks;
        m_waiting = false;
        fail(line, QStringLiteral("timed out after %1 ms: %2").arg(timeoutMs).arg(detail));
        return 0;
    }

    fail(line, "unknown or incomplete step");
    return 0;
}

bool ScriptRunner::check(const QStringList &words, QString *detail) const
{
    const QString key = words[0];
    const QString op = words[1];
    const QString expected = words.mid(2).join(' ');
    const QVariantMap s = state();
    if (!s.contains(key)) {
        *detail = QStringLiteral("no state key '%1'").arg(key);
        return false;
    }
    const QString actual = s.value(key).toString();
    bool numA = false, numE = false;
    const double a = actual.toDouble(&numA);
    const double e = expected.toDouble(&numE);
    const bool numeric = numA && numE;

    bool ok = false;
    if (op == "=") ok = numeric ? a == e : actual == expected;
    else if (op == "!=") ok = numeric ? a != e : actual != expected;
    else if (op == ">=") ok = numeric && a >= e;
    else if (op == "<=") ok = numeric && a <= e;
    else if (op == "~") ok = actual.startsWith(expected);
    *detail = QStringLiteral("%1 is '%2'").arg(key, actual);
    return ok;
}

QVariantMap ScriptRunner::state() const
{
    auto *live = LiveState::instance();
    auto *video = VideoEngine::instance();
    QVariantMap s{
        {"slide", live->index() + 1},
        {"text-cleared", live->textCleared()},
        {"background", video->backgroundVisible()},
        {"video-mode", video->mode()},
        {"video-status", video->status()},
        {"video-source", QFileInfo(video->source().toLocalFile()).fileName()},
        {"loop-gap-ms", video->lastLoopGapMs()},
        {"loops", video->loopCount()},
    };
    const QVariantMap placement = OutputController::instance()->placement();
    for (auto it = placement.cbegin(); it != placement.cend(); ++it)
        s["placement." + it.key()] = it.value();
    for (const QVariant &v : FrameStats::instance()->windows()) {
        const QVariantMap row = v.toMap();
        const QString name = row["name"].toString();
        s["fps." + name] = row["fps"];
        s["late." + name] = row["late"];
        s["frames." + name] = row["totalFrames"];
        s["exposed." + name] = row["exposed"];
        s["last-latency-ms." + name] = row["latencyMs"];
    }
    return s;
}

// grabWindow renders the scene again offscreen, so the picture is what the window
// shows even if another window is partly on top of it.
void ScriptRunner::snapshot(const QString &name)
{
    ++m_snapshots;
    QStringList saved;
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
        QQuickWindow *window = it.value();
        if (!window || !window->isVisible())
            continue;
        const QString path = QStringLiteral("%1/snapshots/%2-%3-%4.png")
                                 .arg(m_outDir).arg(m_snapshots, 2, 10, QChar('0')).arg(name, it.key());
        if (window->grabWindow().save(path))
            saved << it.key();
    }
    QStringList stateText;
    const QVariantMap s = state();
    for (auto it = s.cbegin(); it != s.cend(); ++it)
        stateText << it.key() + "=" + it.value().toString();
    EventLog::write(QStringLiteral("snapshot %1 (%2): %3").arg(name, saved.join(','), stateText.join("  ")));
}
