#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QStringList>
#include <QtTest>
#include <cstdio>
#include <optional>

// The test binary run again as a second Visualis process (#109). The child prints one ready
// line, "<tag>" or "<tag> <payload>", then stays until it's killed or the test that started it
// is gone.
namespace childprocess {

// Starts this binary with `arguments` and waits for the child's `tag` line. Returns its
// payload (empty if it had none), or nothing if the child exited or timed out first.
inline std::optional<QByteArray> start(QProcess &child, const QStringList &arguments, const QByteArray &tag)
{
    child.start(QCoreApplication::applicationFilePath(), arguments);
    if (!child.waitForStarted(5000))
        return std::nullopt;
    for (QDeadlineTimer deadline(15000); !deadline.hasExpired();) {
        while (child.canReadLine()) {
            const QByteArray line = child.readLine().trimmed();
            if (line == tag)
                return QByteArray();
            if (line.startsWith(tag + ' '))
                return line.mid(tag.size() + 1);
        }
        if (!child.waitForReadyRead(int(deadline.remainingTime())) && child.state() == QProcess::NotRunning)
            break;
    }
    qWarning() << "the child never got ready:" << child.readAllStandardError();
    return std::nullopt;
}

// In the child: prints the ready line and waits until stdin closes, which happens when the
// test that started it exits or crashes, so no stray process is left holding a library.
inline int readyUntilParentGoes(const QByteArray &tag, const QByteArray &payload = {})
{
    std::fputs((payload.isEmpty() ? tag : tag + ' ' + payload).constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    while (std::getchar() != EOF) {
    }
    return 0;
}

} // namespace childprocess

// The test's main(): Qt's own QTEST_MAIN wrapper and app `setup` (QTEST_MAIN_SETUP() as
// QTEST_MAIN uses, or QTEST_QAPP_SETUP(QCoreApplication) as QTEST_GUILESS_MAIN does), so the
// tests run exactly as they would under those. With `flag` and `args` more arguments, the
// binary runs `child(argv)` instead of the tests.
#define CHILDPROCESS_TEST_MAIN(TestObject, setup, flag, args, child)          \
    QTEST_MAIN_WRAPPER(TestObject, setup                                      \
                       if (argc == 2 + (args) && qstrcmp(argv[1], flag) == 0) \
                           return child(argv);)
