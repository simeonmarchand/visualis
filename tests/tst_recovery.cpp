#include "live/livejournal.h"
#include "live/livestate.h"
#include "library/songlibrary.h"
#include "template/templatejson.h"
#include "live/recovery.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "childprocess.h"

using namespace visualis;

namespace {

// A song taken live from the library, through the built-in template.
LiveItem libraryItem(const Song &song, const QUuid &arrangement = {})
{
    return liveSong(song, arrangement, templateSet({}));
}

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

// A stored template, as TemplateLibrary loads it.
Template stored(const QString &font)
{
    Template t = builtInLyricsTemplate();
    t.id = QUuid::createUuid();
    t.name = font;
    t.regions[0].style.fontFamily = font;
    t.transitions.content = {TransitionKind::Fade, 250};
    return *templateFromJson(templateToJson(t)).tmpl;
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

// The child side of the --crash-live test: opens the library in argv[2], takes a song live
// with the journal in argv[3] running, says "live <song id>", and waits to be killed.
int crashLiveChild(char **argv)
{
    SongLibrary songs;
    if (!songs.openLibrary(QString::fromLocal8Bit(argv[2])) || !songs.createFromPaste("Joy to the World", kLyrics))
        return 1;
    const std::optional<Song> song = songs.loadSong(QUuid::fromString(songs.selectedId()));
    LiveState live;
    Recovery recovery;
    recovery.start(QString::fromLocal8Bit(argv[3]), &live);
    if (!song || !live.show(libraryItem(*song), 1))
        return 1;
    return childprocess::readyUntilParentGoes("live", songs.selectedId().toUtf8());
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
        QVERIFY(!recovery.start(m_path, &live));
        QVERIFY(recovery.message().isEmpty());
        QVERIFY(!QFile::exists(m_path));   // nothing live yet
    }

    void everyChangeIsJournaled()
    {
        LiveState live;
        Recovery recovery;
        recovery.start(m_path, &live);
        live.show(libraryItem(sample()), 0);
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
            recovery.start(m_path, &live);
            live.show(libraryItem(sample()), 0);
            live.next();
            live.next();
            live.clearBackground();
        }   // no finish(): the process died

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
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
        const QList<LiveItem> items{liveItem(a, sample(), templateSet({})), liveItem(b, second, templateSet({}))};
        const QUuid service = QUuid::createUuid();
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.setService(service, items);
            QVERIFY(live.goToItem(0, 1));
        }   // no finish(): the process died

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
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
        const std::optional<QByteArray> live = childprocess::start(crashed, {"--crash-live", folder, m_path}, "live");
        QVERIFY(live);
        const QString songId = QString::fromUtf8(*live);
        crashed.kill();   // SIGKILL: no destructors, no unlock, no finish()
        QVERIFY(crashed.waitForFinished(5000));
        QVERIFY(QFile::exists(QDir(folder).filePath("library.lock")));

        SongLibrary songs;
        QVERIFY2(songs.openLibrary(folder), qPrintable(songs.error()));
        LiveState restored;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &restored));
        QCOMPARE(restored.songId(), songId);
        songs.select(restored.songId());
        QCOMPARE(songs.selectedId(), songId);
        QCOMPARE(songs.selectedSlides().size(), 2);
    }

    // #93: what comes back is what was on screen, rendered again from the journal alone (the
    // library may not open): the item, its overrides, its template.
    void recoveryRendersTheSameItemFromTheJournalAlone()
    {
        const Template t = stored("Georgia");
        ServiceItem item{QUuid::createUuid(), ServiceItem::Kind::Song, "Joy", {}, {}, {}, {}, {}, {}, {}, {}};
        item.overrides = {{"template", t.id.toString(QUuid::WithoutBraces)},
                          {"regions", QJsonObject{{"main", QJsonObject{{"weight", 800}}}}}};
        item.customSequence = {"Intro", "Verse 1", "Chorus"};
        Song song = sample();
        song.groups[1].slideOverrides = {QJsonObject{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}}};
        const LiveItem shown = liveItem(item, song, templateSet({t}));
        const QUuid service = QUuid::createUuid();
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.setService(service, {shown});
            QVERIFY(live.goToItem(0, 2));
        }   // no finish(): the process died

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.item(), shown);
        QCOMPARE(live.slideIndex(), 2);
        QCOMPARE(live.serviceId(), service);
        QCOMPARE(live.content().regions["main"].style.fontFamily, QStringLiteral("Georgia"));
    }

    void aLibrarySongComesBackInItsTemplate()
    {
        const Template t = stored("Avenir");
        Song song = sample();
        song.defaultTemplate = t.id;
        const LiveItem shown = liveSong(song, {}, templateSet({t}));
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.show(shown, 1);
        }
        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.item(), shown);
        QCOMPARE(live.content().regions["main"].style.fontFamily, QStringLiteral("Avenir"));
    }

    // #98: the background layer is an asset id in LiveState, so it comes back with the rest.
    void theBackgroundComesBackToo()
    {
        const QUuid still = QUuid::createUuid();
        const Transition fade{TransitionKind::Fade, 700};
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.setBackground(still, fade);   // a background with no song live
            live.clearBackground();
        }
        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.background(), (BackgroundLayer{still, true, fade}));
        QVERIFY(!live.hasSlide());
        QVERIFY(recovery.message().contains("the background"));
    }

    void aSongAndItsBackgroundComeBackTogether()
    {
        const QUuid still = QUuid::createUuid();
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.show(libraryItem(sample()), 1);
            live.setBackground(still, {});
            live.next();
        }
        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.slideIndex(), 2);
        QCOMPARE(live.background().asset, still);
    }

    // A journal from before #98 held the video's file URL. Its background isn't restored; the
    // rest is.
    void aJournalWithABackgroundUrlRestoresTheRest()
    {
        LiveSnapshot old;
        old.song = sample();
        old.slideIndex = 1;
        old.background.cleared = true;
        old.savedAt = QDateTime::currentDateTimeUtc();
        QByteArray json = snapshotToJson(old);
        json.replace("\"backgroundCleared\"", "\"background\":\"file:///Users/op/blue.mp4\",\"backgroundCleared\"");
        QVERIFY(snapshotFromJson(json).snapshot->skippedBackgroundUrl);
        QFile f(m_path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(json) == json.size());
        f.close();

        LiveState live;
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.slideIndex(), 1);
        QCOMPARE(live.background().asset, QUuid());
    }

    // A restored background whose file has gone since is left out, with the alert, so the
    // output shows black only because nothing was ever there to show.
    void aRestoredBackgroundWhoseFileIsGoneRaisesTheAlert()
    {
        const QUuid gone = QUuid::createUuid();
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.setBackground(gone, {});
        }
        LiveState live;
        live.setMediaCheck([](const QUuid &) {
            MediaCheck c;
            c.status = MediaStatus::Missing;
            c.message = "\"Sunrise.jpg\" is missing from the media folder.";
            return c;
        });
        Recovery recovery;
        QVERIFY(recovery.start(m_path, &live));
        QCOMPARE(live.background().asset, QUuid());
        QVERIFY(live.backgroundAlert().contains("Sunrise.jpg"));
    }

    void aCleanQuitLeavesNothingToRestore()
    {
        {
            LiveState live;
            Recovery recovery;
            recovery.start(m_path, &live);
            live.show(libraryItem(sample()), 1);
            recovery.finish();
            live.next();   // after finish: no longer journaled
        }
        QVERIFY(!QFile::exists(m_path));
        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start(m_path, &live));
        QVERIFY(!live.hasSlide());
    }

    // #93 review: a journal from before #93 holds the rendered song and its "This service"
    // order, but no service item. It restores, and a second crash before the service is
    // opened again brings back the same order and slide.
    void aJournalFromBefore93SurvivesTwoCrashesInARow()
    {
        ServiceItem item{QUuid::createUuid(), ServiceItem::Kind::Song, "Joy", {}, {}, {}, {}, {}, {}, {}, {}};
        item.customSequence = {"Intro", "Chorus", "Verse 1", "Chorus"};
        const LiveItem shown = liveItem(item, sample(), templateSet({}));
        QCOMPARE(shown.arrangement, item.id);
        LiveSnapshot old;
        old.song = shown.song;
        old.arrangement = shown.arrangement;
        old.item = item.id;
        old.service = QUuid::createUuid();
        old.slideIndex = 3;
        old.savedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(writeJournal(m_path, old));

        for (int crash = 0; crash < 2; ++crash) {
            LiveState live;
            Recovery recovery;
            QVERIFY(recovery.start(m_path, &live));
            QCOMPARE(live.slides(), shown.slides);
            QCOMPARE(live.slideIndex(), 3);
            QCOMPARE(live.itemId(), item.id);
        }   // no finish(), twice
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
        QVERIFY(!recovery.start(m_path, &live));
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
        recovery.start(m_path, &live);
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
        QVERIFY(!recovery.start(m_path, &live));
        live.show(libraryItem(sample()), 0);
        QVERIFY(readJournal(m_path).snapshot);
    }

    void anUnwritableJournalDoesNotStopTheShow()
    {
        LiveState live;
        Recovery recovery;
        QVERIFY(!recovery.start("/dev/null/cannot/live-journal.json", &live));
        QVERIFY(live.show(libraryItem(sample()), 0));
        QVERIFY(live.next());
    }

    void showingNothingRemovesTheJournal()
    {
        LiveState live;
        Recovery recovery;
        recovery.start(m_path, &live);
        live.show(libraryItem(sample()), 0);
        QVERIFY(QFile::exists(m_path));
        live.restore(LiveItem{}, -1, false);
        QVERIFY(!QFile::exists(m_path));
    }

private:
    QTemporaryDir m_dir;
    QString m_path;
    int m_n = 0;
};

// With --crash-live <library> <journal>, this binary stands in for a Visualis that goes on to
// crash (#109).
CHILDPROCESS_TEST_MAIN(RecoveryTest, QTEST_MAIN_SETUP(), "--crash-live", 2, crashLiveChild)
#include "tst_recovery.moc"
