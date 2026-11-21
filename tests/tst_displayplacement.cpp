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

DisplayChoice at(const QString &key, int x)
{
    return {key, QPoint(x, 0)};
}

Status statusOf(const QString &role, const QMap<QString, DisplayChoice> &choices, const QList<DisplayInfo> &displays)
{
    return placeOutputs(kRoles, choices, displays).value(role).status;
}

} // namespace

class DisplayPlacementTest : public QObject
{
    Q_OBJECT

private slots:
    void placesOnTheChosenDisplay()
    {
        const auto p = placeOutputs(kRoles, {{"audience", at("B", 1920)}}, {display("A", 0, true), display("B", 1920)});
        QCOMPARE(p.value("audience").status, Status::Placed);
        QCOMPARE(p.value("audience").display.key, QStringLiteral("B"));
        QCOMPARE(p.value("audience").display.geometry, QRect(1920, 0, 1920, 1080));
        QCOMPARE(p.value("stage").status, Status::NoDisplayChosen);
    }

    void aUniqueDisplayIsFoundWhereverItMoved()
    {
        // Saved at x=1920; the displays were rearranged since.
        const auto p = placeOutputs(kRoles, {{"audience", at("B", 1920)}}, {display("A", 0, true), display("B", 5000)});
        QCOMPARE(p.value("audience").display.geometry.x(), 5000);
    }

    void waitsForAMissingDisplayInsteadOfMoving()
    {
        QCOMPARE(statusOf("audience", {{"audience", at("gone", 1920)}}, {display("A", 0, true), display("B", 1920)}),
                 Status::DisplayMissing);
    }

    // #20: a choice on the operator screen used to leave the output waiting forever.
    void operatorScreenChoiceFallsBackToAFreeDisplay()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("right", 3840), display("left", 1920)};
        const auto p = placeOutputs(kRoles, {{"audience", at("op", 0)}}, displays);
        QCOMPARE(p.value("audience").status, Status::PlacedInstead);
        QCOMPARE(p.value("audience").display.key, QStringLiteral("left"));
    }

    void operatorScreenFallbackSkipsDisplaysOtherOutputsUse()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 1920), display("C", 3840)};
        const auto p = placeOutputs(kRoles, {{"audience", at("op", 0)}, {"stage", at("B", 1920)}}, displays);
        QCOMPARE(p.value("stage").display.key, QStringLiteral("B"));
        QCOMPARE(p.value("audience").display.key, QStringLiteral("C"));
    }

    void operatorScreenChoiceWaitsWhenNothingIsFree()
    {
        QCOMPARE(statusOf("audience", {{"audience", at("op", 0)}, {"stage", at("B", 1920)}},
                          {display("op", 0, true), display("B", 1920)}),
                 Status::OperatorScreen);
    }

    // #20: identical monitors without serials share a key; position tells them apart.
    void identicalDisplaysAreToldApartByPosition()
    {
        const QList<DisplayInfo> twins{display("op", 0, true), display("W156", 1920), display("W156", 3840)};
        const auto p = placeOutputs(kRoles, {{"audience", at("W156", 3840)}, {"stage", at("W156", 1920)}}, twins);
        QCOMPARE(p.value("audience").display.geometry.x(), 3840);
        QCOMPARE(p.value("stage").display.geometry.x(), 1920);
    }

    void withOneTwinUnpluggedTheOtherOutputStaysPut()
    {
        // The right-hand twin (stage's) is unplugged. Audience keeps the left one; stage
        // waits instead of taking it.
        const QList<DisplayInfo> displays{display("op", 0, true), display("W156", 1920)};
        const auto p = placeOutputs(kRoles, {{"audience", at("W156", 1920)}, {"stage", at("W156", 3840)}}, displays);
        QCOMPARE(p.value("audience").status, Status::Placed);
        QCOMPARE(p.value("stage").status, Status::DisplayMissing);
    }

    void twinsMovedTogetherKeepTheirOrder()
    {
        // Both moved 500 px right: each still goes to the nearer one.
        const QList<DisplayInfo> twins{display("op", 0, true), display("W156", 2420), display("W156", 4340)};
        const auto p = placeOutputs(kRoles, {{"audience", at("W156", 1920)}, {"stage", at("W156", 3840)}}, twins);
        QCOMPARE(p.value("audience").display.geometry.x(), 2420);
        QCOMPARE(p.value("stage").display.geometry.x(), 4340);
    }

    void sharedKeysFindsIdenticalDisplays()
    {
        QCOMPARE(sharedKeys({display("op", 0, true), display("W156", 1920), display("W156", 3840), display("B", 5760)}),
                 QStringList{"W156"});
        QVERIFY(sharedKeys({display("op", 0, true), display("B", 1920)}).isEmpty());
    }

    void defaultsGoToFreeDisplaysLeftToRight()
    {
        // Listed out of screen order on purpose: the left-hand display comes first.
        const QList<DisplayInfo> displays{display("op", 0, true), display("right", 3840), display("left", 1920)};
        const auto c = withDefaultDisplays(kRoles, {}, displays);
        QCOMPARE(c.value("audience"), at("left", 1920));
        QCOMPARE(c.value("stage"), at("right", 3840));
    }

    void defaultsGiveIdenticalDisplaysOneEach()
    {
        const QList<DisplayInfo> twins{display("op", 0, true), display("W156", 3840), display("W156", 1920)};
        const auto c = withDefaultDisplays(kRoles, {}, twins);
        QCOMPARE(c.value("audience"), at("W156", 1920));
        QCOMPARE(c.value("stage"), at("W156", 3840));
    }

    void defaultsKeepExistingChoicesAndSkipTakenDisplays()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 1920), display("C", 3840)};
        const auto c = withDefaultDisplays(kRoles, {{"audience", at("B", 1920)}}, displays);
        QCOMPARE(c.value("audience"), at("B", 1920));
        QCOMPARE(c.value("stage"), at("C", 3840));
    }

    void defaultsNeverShareADisplayOrUseTheOperatorScreen()
    {
        const auto c = withDefaultDisplays(kRoles, {}, {display("op", 0, true), display("B", 1920)});
        QCOMPARE(c.value("audience"), at("B", 1920));
        QVERIFY(!c.contains("stage"));
    }

    void defaultsSkipADisplayUsedAsAFallback()
    {
        // Audience chose the operator screen and is on B instead; stage mustn't stack on it.
        const auto c = withDefaultDisplays(kRoles, {{"audience", at("op", 0)}}, {display("op", 0, true), display("B", 1920)});
        QVERIFY(!c.contains("stage"));
    }

    void choosingATakenDisplayFreesItFromTheOtherRole()
    {
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 1920), display("C", 3840)};
        const auto c = withDisplayChosen({{"audience", at("B", 1920)}, {"stage", at("C", 3840)}}, "stage", at("B", 1920), displays);
        QCOMPARE(c.value("stage"), at("B", 1920));
        QVERIFY(!c.contains("audience"));
    }

    void choosingADisplayFreesItEvenIfItMoved()
    {
        // Audience saved B at 1920, but B is now at 5000; choosing it for stage takes it.
        const QList<DisplayInfo> displays{display("op", 0, true), display("B", 5000)};
        const auto c = withDisplayChosen({{"audience", at("B", 1920)}}, "stage", at("B", 5000), displays);
        QVERIFY(!c.contains("audience"));
    }

    void choosingOneTwinLeavesTheOtherAlone()
    {
        const QList<DisplayInfo> twins{display("op", 0, true), display("W156", 1920), display("W156", 3840)};
        const auto c = withDisplayChosen({{"audience", at("W156", 1920)}}, "stage", at("W156", 3840), twins);
        QCOMPARE(c.value("audience"), at("W156", 1920));
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
