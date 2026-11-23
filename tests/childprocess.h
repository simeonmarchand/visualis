#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QStringList>
#include <cstdio>
#include <optional>

// The test binary run again as a second Visualis process (#109). The child prints a ready
// line, then stays until it's killed or the test that started it is gone.
namespace childprocess {

// Starts this binary with `arguments` and waits for the child's line that starts with
// `ready`. Returns the rest of that line, or nothing if it never came.
inline std::optional<QByteArray> start(QProcess &child, const QStringList &arguments, const QByteArray &ready)
{
    child.start(QCoreApplication::applicationFilePath(), arguments);
    if (!child.waitForStarted(5000))
        return std::nullopt;
    QByteArray out;
    for (QDeadlineTimer deadline(15000); !deadline.hasExpired();) {
        if (const qsizetype at = out.indexOf(ready); at >= 0 && out.indexOf('\n', at) > at)
            return out.mid(at + ready.size(), out.indexOf('\n', at) - at - ready.size()).trimmed();
        child.waitForReadyRead(int(deadline.remainingTime()));
        out += child.readAllStandardOutput();
    }
    qWarning() << "the child never got ready:" << child.readAllStandardError();
    return std::nullopt;
}

// In the child: prints `line` and waits until stdin closes, which happens when the test that
// started it exits or crashes, so no stray process is left holding a library.
inline int readyUntilParentGoes(const QByteArray &line)
{
    std::fputs(line.constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    while (std::getchar() != EOF) {
    }
    return 0;
}

} // namespace childprocess
