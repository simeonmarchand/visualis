#include "live/livejournal.h"
#include "live/livestate.h"

#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <limits>

using namespace visualis;

namespace {

Song sample()
{
    Song s;
    s.id = QUuid::createUuid();
    s.title = "Joy to the World";   // public domain
    s.groups = {
        Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Joy to the world", "Let earth receive her King"}},
        Group{QUuid::createUuid(), "Chorus", "#cc004f", {"And heaven and nature sing"}},
    };
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {s.groups[0].id, s.groups[1].id, s.groups[1].id}, {}},
                      Arrangement{QUuid::createUuid(), "Short", {s.groups[1].id}, {}}};
    return s;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class LiveJournalTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsTheLiveState()
    {
        LiveSnapshot s;
        s.song = sample();
        s.arrangement = s.song->arrangements[0].id;
        s.slideIndex = 2;
        s.textCleared = true;
        s.background = "file:///Users/op/Motions/blue.mp4";
        s.savedAt = QDateTime::currentDateTimeUtc();

        const SnapshotResult r = snapshotFromJson(snapshotToJson(s));
        QVERIFY2(r.snapshot, qPrintable(r.error));
        QCOMPARE(r.snapshot->song->title, QStringLiteral("Joy to the World"));
        QCOMPARE(r.snapshot->song->groups, s.song->groups);
        QCOMPARE(r.snapshot->arrangement, s.arrangement);
        QCOMPARE(r.snapshot->slideIndex, 2);
        QVERIFY(r.snapshot->textCleared);
        QVERIFY(!r.snapshot->backgroundCleared);
        QCOMPARE(r.snapshot->background, s.background);
        QCOMPARE(r.snapshot->savedAt.toMSecsSinceEpoch(), s.savedAt.toMSecsSinceEpoch());
        QVERIFY(r.snapshot->service.isNull() && r.snapshot->item.isNull());
    }

    void keepsTheServiceAndItem()
    {
        LiveSnapshot s;
        s.song = sample();
        s.slideIndex = 1;
        s.service = QUuid::createUuid();
        s.item = QUuid::createUuid();
        const SnapshotResult r = snapshotFromJson(snapshotToJson(s));
        QVERIFY2(r.snapshot, qPrintable(r.error));
        QCOMPARE(r.snapshot->service, s.service);
        QCOMPARE(r.snapshot->item, s.item);
    }

    void aBackgroundAloneIsKept()
    {
        LiveSnapshot s;
        s.background = "file:///motion.mov";
        s.backgroundCleared = true;
        const SnapshotResult r = snapshotFromJson(snapshotToJson(s));
        QVERIFY(r.snapshot && !r.snapshot->song);
        QVERIFY(r.snapshot->backgroundCleared);
        QVERIFY(!r.snapshot->isEmpty());
        QVERIFY(LiveSnapshot{}.isEmpty());
    }

    void writesAndReadsAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("live.json");
        QVERIFY(!readJournal(path).snapshot && readJournal(path).error.isEmpty());   // none yet: not an error

        LiveSnapshot s;
        s.song = sample();
        s.slideIndex = 1;
        QString error;
        QVERIFY2(writeJournal(path, s, &error), qPrintable(error));
        QCOMPARE(readJournal(path).snapshot->slideIndex, 1);

        s.slideIndex = 2;   // overwritten in place
        QVERIFY(writeJournal(path, s));
        QCOMPARE(readJournal(path).snapshot->slideIndex, 2);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"live.json"});   // no temp files left
    }

    // Written on the GUI thread on every slide change, so it must stay cheap.
    // A shared CI runner can stall one run (#108), so the whole run gets three tries. Each
    // run is a mean over 100 writes, so a cost that only some writes pay still fails all three.
    void writingIsCheap()
    {
        QTemporaryDir dir;
        LiveSnapshot s;
        s.song = sample();
        constexpr int writes = 100;
        double best = std::numeric_limits<double>::max();
        for (int run = 0; run < 3 && best >= 5; ++run) {
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < writes; ++i) {
                s.slideIndex = i % 3;
                QVERIFY(writeJournal(dir.filePath("live.json"), s));
            }
            best = std::min(best, timer.nsecsElapsed() / 1e6 / writes);
        }
        qInfo() << "journal write:" << best << "ms";
        QVERIFY2(best < 5, qPrintable(QString::number(best)));
    }

    void badJournalsAreRefusedNotFatal_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("garbage") << QByteArray("\x00\xff{{not json", 12);
        QTest::newRow("truncated") << snapshotToJson(LiveSnapshot{sample(), {}, 1}).left(40);
        QTest::newRow("array") << QByteArray("[1,2,3]");
        QTest::newRow("other format") << QByteArray(R"({"format":"visualis-song","version":1})");
        QTest::newRow("newer") << QByteArray(R"({"format":"visualis-live","version":99})");
        QTest::newRow("bad song") << QByteArray(R"({"format":"visualis-live","version":1,"song":{"title":5}})");
    }

    void badJournalsAreRefusedNotFatal()
    {
        QFETCH(QByteArray, json);
        const SnapshotResult r = snapshotFromJson(json);
        QVERIFY(!r.snapshot);
        QVERIFY(!r.error.isEmpty());
    }

    void restoreBringsBackTheSlideAndClears()
    {
        const Song song = sample();
        LiveState live;
        QSignalSpy changed(&live, &LiveState::changed);
        live.restore(song, song.arrangements[0].id, 2, true, true);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(live.slideIndex(), 2);
        QCOMPARE(live.groupLabel(), QStringLiteral("Chorus"));
        QVERIFY(live.textCleared() && live.backgroundCleared());
        QCOMPARE(live.arrangementId(), song.arrangements[0].id);
    }

    void restoreUsesTheSavedArrangement()
    {
        const Song song = sample();
        LiveState live;
        live.restore(song, song.arrangements[1].id, 0, false, false);
        QCOMPARE(live.slideCount(), 1);   // "Short": just the chorus
        QCOMPARE(live.currentText(), QStringLiteral("And heaven and nature sing"));
    }

    void restoreWithAnIndexThatNoLongerFitsShowsNoSlide()
    {
        const Song song = sample();
        LiveState live;
        live.restore(song, {}, 40, false, false);
        QCOMPARE(live.songTitle(), song.title);
        QVERIFY(!live.hasSlide());
    }
};

QTEST_GUILESS_MAIN(LiveJournalTest)
#include "tst_livejournal.moc"
