#pragma once

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStringList>
#include <QVariantMap>

// Plays a test script (--script): one step per line, run in order on the GUI thread.
//
//   next | prev | goto <slide> | clear-text | background on|off | clear-all
//   mode shared|per-window | video <file> | assign <role> <screen key>
//   wait <ms>
//   mark <text>                          logs "mark <text>" so an outside script can sync on it
//   snapshot <name>                      saves a PNG of every visible window
//   locate <objectName>                  logs "located <name> at <x> <y>": the item's centre in
//                                        global screen points, so an outside script can click it
//   expect <key> <op> <value>            op: =  !=  >=  <=  ~ (starts with)  has (contains)
//   wait-for <key> <op> <value> [ms]     polls until true, fails after the timeout (default 5000)
//   # comment
//
// Keys come from state(): slide, text-cleared, background, video-mode, video-status,
// video-source, loops, loop-gap-ms, placement.<role>, fps.<window>, late.<window>, frames.<window>,
// exposed.<window>, last-latency-ms.<window>.
class ScriptRunner : public QObject
{
    Q_OBJECT

public:
    ScriptRunner(const QString &scriptPath, const QString &outDir,
                 const QMap<QString, QQuickWindow *> &windows, QObject *parent = nullptr);

    void start();

private:
    struct Line { int number; QStringList words; };

    void step();
    void finish();
    // Returns the delay before the next step, or -1 to wait for a poll.
    int run(const Line &line);
    bool check(const QStringList &words, QString *detail) const;
    void snapshot(const QString &name);
    QVariantMap state() const;
    void fail(const Line &line, const QString &why);

    QList<Line> m_lines;
    QString m_outDir;
    QMap<QString, QPointer<QQuickWindow>> m_windows;
    int m_pos = 0;
    int m_checks = 0;
    int m_failures = 0;
    int m_snapshots = 0;
    QElapsedTimer m_waitingSince;
    bool m_waiting = false;
};
