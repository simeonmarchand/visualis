#include "live/livejournal.h"
#include "live/livestate.h"
#include "library/songlibrary.h"
#include "live/recovery.h"
#include "video/videoengine.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
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


// One group of `slides` slides, played `steps` times: structurally small, huge expanded.
Song repeated(int slides, int steps)
{
    Song s;
    s.title = "Huge";
    QStringList text;
    for (int i = 0; i < slides; ++i)
        text << QString::number(i);
    s.groups = {Group{QUuid::createUuid(), "Verse", "", text}};
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", QList<QUuid>(steps, s.groups[0].id), {}}};
    return s;
}

// Two slides (public domain).
const char *kLyrics = "Verse 1\nJoy to the world\n\nChorus\nAnd heaven and nature sing\n";

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

    // #51: a crash mid-service comes back to the same item and slide, and Next carries on
    // into the next item once the service is open again.
    void afterACrashMidServiceTheSameItemComesBack()
    {
        Song second = sample();
        second.title = "O Come All Ye Faithful";
        const ServiceItem a{QUuid::createUuid(), ServiceItem::Kind::Song, "Joy", {}, {}, {}, {}, {}, {}, {}, {}};
        const ServiceItem b{QUuid::createUuid(), ServiceItem::Kind::Song, "O Come", {}, {}, {}, {}, {}, {}, {}, {}};
        const QList<LiveItem> items{liveItem(a, sample()), liveItem(b, second)};
        const QUuid service = QUuid::createUuid();
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live, nullptr);
            live.setService(service, items);
            QVERIFY(live.goToItem(0, 1));
        }   // no finish(): the process died

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live, nullptr));
        QCOMPARE(live.serviceId(), service);
        QCOMPARE(live.itemId(), a.id);
        QCOMPARE(live.slideIndex(), 1);
        live.setService(service, items);   // the operator window opens the service again
        QCOMPARE(live.itemIndex(), 0);
        QVERIFY(live.next());
        QVERIFY(live.next());
        QCOMPARE(live.itemIndex(), 1);
        QCOMPARE(live.songTitle(), QStringLiteral("O Come All Ye Faithful"));
        QCOMPARE(readJournal(m_path).snapshot->item, b.id);
    }

    // #109 with #25: a Visualis that's killed mid-service leaves its library lock behind. The
    // restart still opens the library, as main.cpp does, and the live song comes back from it.
    void afterAKillTheLibraryOpensAndTheLiveSongComesBack()
    {
        const QString folder = m_dir.filePath(QString("library-%1").arg(m_n));
        QProcess crashed;
        crashed.start(QCoreApplication::applicationFilePath(), {"--crash-live", folder, m_path});
        QVERIFY(crashed.waitForStarted(5000));
        QByteArray out;
        for (QDeadlineTimer deadline(15000); !out.contains("live ") && !deadline.hasExpired();) {
            crashed.waitForReadyRead(int(deadline.remainingTime()));
            out += crashed.readAllStandardOutput();
        }
        QVERIFY2(out.contains("live "), crashed.readAllStandardError().constData());
        const QString songId = QString::fromUtf8(out.mid(out.indexOf("live ") + 5).trimmed());
        crashed.kill();   // SIGKILL: no destructors, no unlock, no finish()
        QVERIFY(crashed.waitForFinished(5000));
        QVERIFY(QFile::exists(QDir(folder).filePath("library.lock")));

        SongLibrary songs;
        QVERIFY2(songs.openLibrary(folder), qPrintable(songs.error()));
        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live, nullptr));
        QCOMPARE(live.songId(), songId);
        songs.select(live.songId());
        QCOMPARE(songs.selectedId(), songId);
        QCOMPARE(songs.selectedSlides().size(), 2);
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

    // #83: journals are capped at 4 MiB, but what they expand to isn't.
    void anOversizedJournalIsReportedNotRestored()
    {
        LiveSnapshot big;
        big.song = repeated(500, 1000);
        big.slideIndex = 3;
        big.savedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(writeJournal(m_path, big));

        LiveState live;
        Recovery recovery;
        recovery.start(m_path, &live, nullptr);
        QVERIFY(!live.hasSlide());
        QVERIFY(live.songTitle().isEmpty());
        QVERIFY2(recovery.message().contains("wasn't restored") && recovery.message().contains("too many slides"),
                 qPrintable(recovery.message()));
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

// With --crash-live <library> <journal>, this binary stands in for a Visualis that goes on
// to crash (#109): it opens the library, takes a song live with the journal running, prints
// "live <song id>", and waits to be killed.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc == 4 && qstrcmp(argv[1], "--crash-live") == 0) {
        SongLibrary songs;
        if (!songs.openLibrary(QString::fromLocal8Bit(argv[2])) || !songs.createFromPaste("Joy to the World", kLyrics))
            return 1;
        const std::optional<Song> song = songs.loadSong(QUuid::fromString(songs.selectedId()));
        LiveState live;
        Recovery recovery;
        recovery.start(QString::fromLocal8Bit(argv[3]), &live, nullptr);
        if (!song || !live.show(*song, {}, 1))
            return 1;
        std::printf("live %s\n", qPrintable(songs.selectedId()));
        std::fflush(stdout);
        for (;;)
            QThread::sleep(60);
    }
    RecoveryTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_recovery.moc"
