#include "output/displayplacement.h"
#include "output/displaysettle.h"

#include <QSignalSpy>
#include <QtTest>

using namespace visualis;
using namespace std::chrono_literals;
using Status = OutputPlacement::Status;

namespace {

const QStringList kRoles{"audience", "stage"};

DisplayInfo display(const QString &key, int x, bool isOperator = false)
{
    return DisplayInfo{key, "Display " + key, QRect(x, 0, 1920, 1080), isOperator};
}

} // namespace

class DisplayPlacementTest : public QObject
{
    Q_OBJECT

private slots:
    void placesOnTheAssignedDisplay()
    {
        const auto p = placeOutput("B", {display("A", 0, true), display("B", 1920)});
        QCOMPARE(p.status, Status::Placed);
        QCOMPARE(p.display.key, QStringLiteral("B"));
        QCOMPARE(p.display.geometry, QRect(1920, 0, 1920, 1080));
    }

    void neverPlacesOnTheOperatorScreen()
    {
        QCOMPARE(placeOutput("A", {display("A", 0, true), display("B", 1920)}).status, Status::OperatorScreen);
    }

    void waitsForAMissingDisplayInsteadOfMoving()
    {
        QCOMPARE(placeOutput("gone", {display("A", 0, true), display("B", 1920)}).status, Status::DisplayMissing);
    }

    void waitsWhenNothingIsChosen()
    {
        QCOMPARE(placeOutput({}, {display("A", 0, true), display("B", 1920)}).status, Status::NoDisplayChosen);
    }

    void defaultsGoToFreeDisplaysLeftToRight()
    {
        // Listed out of screen order on purpose: the left-hand display comes first.
        const QList<DisplayInfo> displays{display("op", 0, true), display("right", 3840), display("left", 1920)};
        const auto a = withDefaultDisplays(kRoles, {}, displays);
        QCOMPARE(a.value("audience"), QStringLiteral("left"));
        QCOMPARE(a.value("stage"), QStringLiteral("right"));
    }

    void defaultsKeepExistingChoicesAndSkipTakenDisplays()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 1920), display("C", 3840)};
        const auto a = withDefaultDisplays(kRoles, {{"audience", "B"}}, displays);
        QCOMPARE(a.value("audience"), QStringLiteral("B"));
        QCOMPARE(a.value("stage"), QStringLiteral("C"));
    }

    void defaultsNeverShareADisplayOrUseTheOperatorScreen()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 1920)};
        const auto a = withDefaultDisplays(kRoles, {}, displays);
        QCOMPARE(a.value("audience"), QStringLiteral("B"));
        QVERIFY(!a.contains("stage"));
    }

    void aChoiceForAMissingDisplayStillBlocksIt()
    {
        // Audience waits for "B" while it's unplugged; stage mustn't take it in the meantime
        // and then collide when it comes back.
        const auto a = withDefaultDisplays(kRoles, {{"audience", "B"}}, {display("op", 0, true)});
        QVERIFY(!a.contains("stage"));
        const auto back = withDefaultDisplays(kRoles, a, {display("op", 0, true), display("B", 1920)});
        QCOMPARE(back.value("audience"), QStringLiteral("B"));
        QVERIFY(!back.contains("stage"));
    }

    void choosingATakenDisplayFreesItFromTheOtherRole()
    {
        const auto a = withDisplayChosen({{"audience", "B"}, {"stage", "C"}}, "stage", "B");
        QCOMPARE(a.value("stage"), QStringLiteral("B"));
        QVERIFY(!a.contains("audience"));
    }

    void settleWaitsForQuietThenFiresOnce()
    {
        DisplaySettle settle(100ms);
        QSignalSpy settled(&settle, &DisplaySettle::settled);
        QSignalSpy settling(&settle, &DisplaySettle::settlingChanged);

        // A flickering display: changes every 50 ms keep it settling.
        for (int i = 0; i < 5; ++i) {
            settle.changed();
            QTest::qWait(50);
        }
        QVERIFY(settle.isSettling());
        QCOMPARE(settled.count(), 0);

        QVERIFY(settled.wait(1000));
        QCOMPARE(settled.count(), 1);
        QVERIFY(!settle.isSettling());
        QCOMPARE(settling.count(), 2);
        QCOMPARE(settling.at(0).at(0).toBool(), true);
        QCOMPARE(settling.at(1).at(0).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(DisplayPlacementTest)
#include "tst_displayplacement.moc"
