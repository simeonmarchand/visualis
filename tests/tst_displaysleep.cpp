#include "output/platformdisplay.h"

#include <QtTest>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <unistd.h>
#endif

namespace {

// How many display-sleep assertions this process holds, as macOS reports them.
int heldByThisProcess()
{
#ifdef Q_OS_MACOS
    CFDictionaryRef byProcess = nullptr;
    if (IOPMCopyAssertionsByProcess(&byProcess) != kIOReturnSuccess || !byProcess)
        return -1;
    int count = 0;
    const int pid = getpid();
    CFNumberRef key = CFNumberCreate(nullptr, kCFNumberIntType, &pid);
    if (auto list = static_cast<CFArrayRef>(CFDictionaryGetValue(byProcess, key))) {
        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
            auto a = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
            auto type = static_cast<CFStringRef>(CFDictionaryGetValue(a, kIOPMAssertionTypeKey));
            if (type && CFStringCompare(type, kIOPMAssertionTypePreventUserIdleDisplaySleep, 0) == kCFCompareEqualTo)
                ++count;
        }
    }
    CFRelease(key);
    CFRelease(byProcess);
    return count;
#else
    return 0;
#endif
}

} // namespace

// #26: while outputs show, macOS must not idle-sleep the displays. Checked against what the
// OS itself reports, not just our own flag.
class DisplaySleepTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifndef Q_OS_MACOS
        QSKIP("display sleep is only blocked on macOS so far");
#endif
        QCOMPARE(heldByThisProcess(), 0);
    }

    void activeHoldsOneAssertionAndInactiveReleasesIt()
    {
        DisplaySleepBlocker blocker;
        QVERIFY(blocker.setActive(true));
        QVERIFY(blocker.isActive());
        QCOMPARE(heldByThisProcess(), 1);
        QVERIFY(blocker.setActive(true));   // again: still just one
        QCOMPARE(heldByThisProcess(), 1);
        QVERIFY(blocker.setActive(false));
        QVERIFY(!blocker.isActive());
        QCOMPARE(heldByThisProcess(), 0);
    }

    void destroyingItReleasesTheAssertion()
    {
        {
            DisplaySleepBlocker blocker;
            blocker.setActive(true);
            QCOMPARE(heldByThisProcess(), 1);
        }
        QCOMPARE(heldByThisProcess(), 0);
    }
};

QTEST_GUILESS_MAIN(DisplaySleepTest)
#include "tst_displaysleep.moc"
