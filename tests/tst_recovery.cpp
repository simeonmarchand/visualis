#include "live/livejournal.h"
#include "live/livestate.h"
#include "live/recovery.h"
#include "video/videoengine.h"

#include <QTemporaryDir>
#include <QtTest>

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
    return s;
}

} // namespace

// A "crash" here is a Recovery that never gets finish(): the next one finds its journal.
class RecoveryTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); m_path = m_dir.filePath(QString("journal-%1.json").arg(++m_n)); }

    void aFirstStartRestoresNothing()
    {
        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start(m_path, &live, nullptr));
        QVERIFY(recovery.message().isEmpty());
        QVERIFY(!QFile::exists(m_path));   // nothing live yet
    }

    void everyChangeIsJournaled()
    {
        LiveState live;
        Recovery recovery;
        recovery.start(m_path, &live, nullptr);
        live.show(sample(), {}, 0);
        QCOMPARE(readJournal(m_path).snapshot->slideIndex, 0);
        live.next();
        QCOMPARE(readJournal(m_path).snapshot->slideIndex, 1);
        live.clearText();
        QVERIFY(readJournal(m_path).snapshot->textCleared);
    }

    void afterACrashTheSameSlideComesBack()
    {
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live, nullptr);
            live.show(sample(), {}, 0);
            live.next();
            live.next();
            live.clearBackground();
        }   // no finish(): the process died

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live, nullptr));
        QCOMPARE(live.songTitle(), QStringLiteral("Joy to the World"));
        QCOMPARE(live.slideIndex(), 2);
        QCOMPARE(live.currentText(), QStringLiteral("And heaven and nature sing"));
        QVERIFY(live.backgroundCleared());
        QVERIFY(recovery.message().contains("slide 3 of 3"));
        recovery.dismiss();
        QVERIFY(recovery.message().isEmpty());
    }

    void theBackgroundComesBackToo()
    {
        const QUrl clip = QUrl::fromLocalFile(m_dir.filePath("motion.mp4"));
        {
            LiveState live;
            VideoEngine video;
            Recovery recovery;
            recovery.start(m_path, &live, &video);
            video.setSource(clip);   // a background with no song live
        }
        LiveState live;
        VideoEngine video;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live, &video));
        QCOMPARE(video.source(), clip);
        QVERIFY(!live.hasSlide());
    }

    void aCleanQuitLeavesNothingToRestore()
    {
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live, nullptr);
            live.show(sample(), {}, 1);
            recovery.finish();
            live.next();   // after finish: no longer journaled
        }
        QVERIFY(!QFile::exists(m_path));
        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start(m_path, &live, nullptr));
        QVERIFY(!live.hasSlide());
    }

    void aJournalFromAnotherDayIsNotRestored()
    {
        LiveSnapshot old;
        old.song = sample();
        old.slideIndex = 1;
        old.savedAt = QDateTime::currentDateTimeUtc().addSecs(-(Recovery::kMaxAgeHours * 3600 + 60));
        QVERIFY(writeJournal(m_path, old));

        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start(m_path, &live, nullptr));
        QVERIFY(!live.hasSlide());
    }

    void aCorruptJournalIsIgnoredAndReplaced()
    {
        QFile f(m_path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"format\":\"visualis-live\",\"song\":{");   // cut off mid-write
        f.close();

        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start(m_path, &live, nullptr));
        live.show(sample(), {}, 0);
        QVERIFY(readJournal(m_path).snapshot);
    }

    void anUnwritableJournalDoesNotStopTheShow()
    {
        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start("/dev/null/cannot/live-journal.json", &live, nullptr));
        QVERIFY(live.show(sample(), {}, 0));
        QVERIFY(live.next());
    }

    void showingNothingRemovesTheJournal()
    {
        LiveState live;
        Recovery recovery;
        recovery.start(m_path, &live, nullptr);
        live.show(sample(), {}, 0);
        QVERIFY(QFile::exists(m_path));
        live.restore(Song{}, {}, -1, false, false);
        QVERIFY(!QFile::exists(m_path));
    }

private:
    QTemporaryDir m_dir;
    QString m_path;
    int m_n = 0;
};

QTEST_MAIN(RecoveryTest)
#include "tst_recovery.moc"
