#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "library/library.h"
#include "pco/planimport.h"
#include "pco/songlink.h"
#include "service/servicestore.h"
#include "service/servicejson.h"
#include "song/songjson.h"
#include "song/songstore.h"
#include "live/livestate.h"

#include <QQmlApplicationEngine>
#include <QSqlDatabase>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include "testkeychain.h"

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

const char *kLyrics =
    "Verse 1\nAmazing grace how sweet the sound\nThat saved a wretch like me\n"
    "I once was lost but now am found\nWas blind but now I see\n\n"
    "Chorus\nMy chains are gone\nI've been set free\n";

QStringList titles(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &s : list)
        out << s.toMap().value("title").toString();
    return out;
}

} // namespace

class OperatorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { useTestKeychainEntry(); }

    void init()
    {
        QVERIFY(m_dir.isValid());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_songs = m_engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
        m_services = m_engine->singletonInstance<ServiceList *>("Visualis.Ui", "ServiceList");
        QVERIFY(m_songs && m_live && m_services);
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(++m_libraries))));
    }

    void cleanup() { m_engine.reset(); }

    void aNewLibraryIsEmpty()
    {
        QVERIFY(m_songs->error().isEmpty());
        QVERIFY(m_songs->songs().isEmpty());
        QVERIFY(m_songs->selectedSlides().isEmpty());
    }

    void pastedLyricsBecomeASelectedSongWithTwoLineSlides()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QCOMPARE(titles(m_songs->songs()), QStringList{"Amazing Grace"});
        QCOMPARE(m_songs->selectedTitle(), QStringLiteral("Amazing Grace"));
        const QVariantList slides = m_songs->selectedSlides();
        QCOMPARE(slides.size(), 3);   // the 4-line verse splits in two
        QCOMPARE(slides[0].toMap().value("text").toString(),
                 QStringLiteral("Amazing grace how sweet the sound\nThat saved a wretch like me"));
        QCOMPARE(slides[2].toMap().value("groupLabel").toString(), QStringLiteral("Chorus"));
        QVERIFY(!slides[2].toMap().value("groupColor").toString().isEmpty());
        QVERIFY(m_songs->report().contains("Created"));
    }

    void pastingNeedsATitleAndLyrics()
    {
        QVERIFY(!m_songs->createFromPaste("", kLyrics));
        QVERIFY(!m_songs->createFromPaste("Title", "  \n"));
        QVERIFY(m_songs->songs().isEmpty());
        QVERIFY(!m_songs->report().isEmpty());
    }

    void searchMatchesEveryWordIgnoringCase()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QVERIFY(m_songs->createFromPaste("Grace Alone", kLyrics));
        QVERIFY(m_songs->createFromPaste("Holy Spirit", kLyrics));
        m_songs->setSearch("grace");
        QCOMPARE(titles(m_songs->songs()), (QStringList{"Amazing Grace", "Grace Alone"}));
        m_songs->setSearch("GRACE amaz");
        QCOMPARE(titles(m_songs->songs()), QStringList{"Amazing Grace"});
        m_songs->setSearch("");
        QCOMPARE(m_songs->songs().size(), 3);
    }

    void importsVisualisSongExports()
    {
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(FIXTURES_DIR "/songs")));
        QVERIFY(waitForImport());
        QCOMPARE(titles(m_songs->songs()), (QStringList{"Amazing Grace", "Holy, Holy, Holy"}));
        QVERIFY(m_songs->report().startsWith("Imported 2 songs"));
    }

    // #68: what the import noticed reaches the operator, not just the counts.
    void songImportWarningsAreReported()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));   // same title, another song
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(FIXTURES_DIR "/songs")));
        QVERIFY(waitForImport());
        QVERIFY2(m_songs->report().contains("Amazing Grace") && m_songs->report().contains("both are kept"),
                 qPrintable(m_songs->report()));
    }

    void proPresenterImportWarningsAreReported()
    {
        QVERIFY(m_songs->importProPresenter(QUrl::fromLocalFile(FIXTURES_DIR "/propresenter")));
        QVERIFY(waitForImport());
        QVERIFY2(m_songs->report().startsWith("Imported 1 songs from ProPresenter."), qPrintable(m_songs->report()));
        QVERIFY2(m_songs->report().contains("Missing Group.pro: 1 arrangement step(s) pointed at groups that don't exist"),
                 qPrintable(m_songs->report()));
    }

    void importReportEndsWithTheWarnings()
    {
        const SongLibrary::ImportSummary s{{}, {"A"}, {}, {{"bad.json", "truncated"}}, {"a.json: one", "b.json: two"}};
        QCOMPARE(SongLibrary::importReport(s),
                 QStringLiteral("Imported 1 songs.\nCouldn't read bad.json: truncated\na.json: one\nb.json: two"));
    }

    void aLongReportIsSummarized()
    {
        QCOMPARE(SongLibrary::summarizeReport("a\nb\nc", 6), QStringLiteral("a\nb\nc"));
        QCOMPARE(SongLibrary::summarizeReport("1\n2\n3\n4\n5\n6", 6), QStringLiteral("1\n2\n3\n4\n5\n6"));
        QCOMPARE(SongLibrary::summarizeReport("1\n2\n3\n4\n5\n6\n7\n8\n9", 6),
                 QStringLiteral("1\n2\n3\n4\n5\n…and 4 more (see Details)"));
    }

    // Hundreds of warnings stay a few lines in the narrow pane, with the full list behind
    // Details.
    void manyWarningsDontSwampTheLibraryPane()
    {
        // The same 300 titles twice, as different songs: every one of the second set clashes.
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(writeSongFiles(300))));
        QVERIFY(waitForImport());
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(writeSongFiles(300))));
        QVERIFY(waitForImport());
        QVERIFY2(m_songs->report().count("both are kept") == 300, qPrintable(m_songs->report().left(300)));

        QQuickWindow *window = loadOperatorWindow();
        auto *label = window->findChild<QQuickItem *>("libraryReport");
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), m_songs->reportSummary());
        QVERIFY(m_songs->reportSummary().contains("…and 296 more"));
        QVERIFY(label->property("lineCount").toInt() <= 10);
        QVERIFY2(label->height() < 250, qPrintable(QString::number(label->height())));

        auto *details = window->findChild<QQuickItem *>("libraryReportDetails");
        QVERIFY(details && details->isVisible());
        click(window, details);
        auto *dialog = window->findChild<QObject *>("libraryReportDialog");
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(window->findChild<QObject *>("libraryReportText")->property("text").toString(), m_songs->report());
    }

    void aShortReportHasNoDetailsButton()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QQuickWindow *window = loadOperatorWindow();
        QVERIFY(!window->findChild<QQuickItem *>("libraryReportDetails")->isVisible());
    }

    void goLiveShowsTheSelectedSongsSlide()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QVERIFY(m_songs->goLive(1));
        QCOMPARE(m_live->songId(), m_songs->selectedId());
        QCOMPARE(m_live->slideIndex(), 1);
        QCOMPARE(m_live->currentText(), QStringLiteral("I once was lost but now am found\nWas blind but now I see"));
        QVERIFY(!m_songs->goLive(99));
    }

    void aLibraryThatCantOpenIsReportedNotFatal()
    {
        QFile file(m_dir.filePath("not-a-folder"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(!m_songs->openLibrary(file.fileName()));
        QVERIFY(!m_songs->error().isEmpty());
        QVERIFY(m_songs->songs().isEmpty());
        QVERIFY(!m_songs->createFromPaste("Amazing Grace", kLyrics));
    }

    void clickingATileGoesLiveAndHighlightsCurrentAndNext()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QQuickWindow *window = loadOperatorWindow();

        QQuickItem *first = tile(window, 0);
        QVERIFY(first);
        QVERIFY(!first->property("isCurrent").toBool());
        click(window, first);
        QCOMPARE(m_live->slideIndex(), 0);
        QTRY_VERIFY(tile(window, 0)->property("isCurrent").toBool());
        QVERIFY(tile(window, 1)->property("isNext").toBool());
        QVERIFY(!tile(window, 2)->property("isNext").toBool());

        click(window, tile(window, 2));
        QCOMPARE(m_live->slideIndex(), 2);
        QTRY_VERIFY(tile(window, 2)->property("isCurrent").toBool());
        QVERIFY(!tile(window, 0)->property("isCurrent").toBool());

        // VISUALIS_SNAPSHOT_DIR saves a picture of the window for a look by eye.
        if (const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
            click(window, tile(window, 1));
            QTest::qWait(300);
            window->grabWindow().save(dir + "/operator.png");
        }
    }

    void anotherSongSelectedShowsNoLiveHighlight()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QVERIFY(m_songs->goLive(0));
        QVERIFY(m_songs->createFromPaste("Grace Alone", kLyrics));   // selects the new song
        QQuickWindow *window = loadOperatorWindow();
        QVERIFY(!tile(window, 0)->property("isCurrent").toBool());
    }

    // #24: keys, as an operator or a clicker sends them.
    void keysStepThroughSlides()
    {
        // Three slides, live at the first. Each key, then where it should land.
        QQuickWindow *window = liveOperatorWindow();
        const QList<std::pair<Qt::Key, int>> steps{
            {Qt::Key_Right, 1}, {Qt::Key_Down, 2}, {Qt::Key_Space, 2},   // the end: stays
            {Qt::Key_PageUp, 1}, {Qt::Key_Left, 0}, {Qt::Key_Up, 0},     // the start: stays
            {Qt::Key_Space, 1}, {Qt::Key_PageDown, 2},
        };
        for (const auto &[key, expected] : steps) {
            QTest::keyClick(window, key);
            QCOMPARE(m_live->slideIndex(), expected);
        }
    }

    void keysClearLayers()
    {
        QQuickWindow *window = liveOperatorWindow();
        QTest::keyClick(window, Qt::Key_C);
        QVERIFY(m_live->textCleared());
        QVERIFY(!m_live->backgroundCleared());
        QTest::keyClick(window, Qt::Key_Right);   // the next slide shows text again
        QVERIFY(!m_live->textCleared());
        QTest::keyClick(window, Qt::Key_B);
        QVERIFY(m_live->backgroundCleared());
        QVERIFY(!m_live->textCleared());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(m_live->textCleared() && m_live->backgroundCleared());
    }

    void shiftLetterJumpsToAGroup()
    {
        QQuickWindow *window = liveOperatorWindow();
        QTest::keyClick(window, Qt::Key_C, Qt::ShiftModifier);
        QCOMPARE(m_live->groupLabel(), QStringLiteral("Chorus"));
        QTest::keyClick(window, Qt::Key_V, Qt::ShiftModifier);   // wraps back to the verse
        QCOMPARE(m_live->groupLabel(), QStringLiteral("Verse 1"));
        QCOMPARE(m_live->slideIndex(), 0);
    }

    void typingInSearchDoesNotDriveSlides()
    {
        QQuickWindow *window = liveOperatorWindow();
        auto *search = window->findChild<QQuickItem *>("songSearch");
        click(window, search);
        QTRY_VERIFY(search->hasActiveFocus());
        for (char ch : {'c', ' ', 'b'})
            QTest::keyClick(window, ch);
        QCOMPARE(search->property("text").toString(), QStringLiteral("c b"));
        QVERIFY(!m_live->textCleared() && !m_live->backgroundCleared());
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(m_live->slideIndex(), 0);
        QTest::keyClick(window, Qt::Key_Escape);   // the panic key still works
        QVERIFY(m_live->textCleared());
    }

    // P1 keyboard test row 8: after a click, the keys drive slides, not what was clicked.
    void afterClickingATileKeysStillDriveSlides()
    {
        QQuickWindow *window = liveOperatorWindow();
        click(window, window->findChild<QQuickItem *>("songSearch"));
        click(window, tile(window, 1));
        QCOMPARE(m_live->slideIndex(), 1);
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(m_live->slideIndex(), 2);
    }

    // #51: services.
    void servicesAreCreatedRenamedAndDeleted()
    {
        QVERIFY(m_services->services().isEmpty());
        QVERIFY(m_services->openId().isEmpty());
        QVERIFY(!m_services->createService("  "));
        QVERIFY(m_services->createService("Sunday AM"));
        QCOMPARE(m_services->openTitle(), QStringLiteral("Sunday AM"));
        QCOMPARE(m_live->serviceIdString(), m_services->openId());
        QVERIFY(m_services->createService("Christmas Eve"));
        QVERIFY(m_services->renameService("Christmas Eve 2026"));
        QCOMPARE(titles(m_services->services()), (QStringList{"Christmas Eve 2026", "Sunday AM"}));

        const QString christmas = m_services->openId();
        QVERIFY(m_services->openService(m_services->services()[1].toMap().value("id").toString()));
        QCOMPARE(m_services->openTitle(), QStringLiteral("Sunday AM"));
        QVERIFY(m_services->removeService(christmas));
        QCOMPARE(titles(m_services->services()), QStringList{"Sunday AM"});
        QCOMPARE(m_services->openTitle(), QStringLiteral("Sunday AM"));   // another service: still open
        QVERIFY(m_services->removeService(m_services->openId()));
        QVERIFY(m_services->openId().isEmpty());
        QVERIFY(m_live->serviceIdString().isEmpty());
    }

    void itemsGoAfterTheSelectionAndSaveAsTheyChange()
    {
        const QStringList songs = makeSongs({"Amazing Grace", "Holy Spirit"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(songs[0]));
        QVERIFY(m_services->addSong(songs[1]));
        m_services->select(0);
        QVERIFY(m_services->addHeader("Welcome"));   // after Amazing Grace, and selected
        QCOMPARE(m_services->selectedIndex(), 1);
        QVERIFY(m_services->addText("", ""));
        QVERIFY(!m_services->addHeader(" "));
        QVERIFY(!m_services->addSong("not-a-song"));
        QCOMPARE(titles(m_services->items()), (QStringList{"Amazing Grace", "Welcome", "Blank", "Holy Spirit"}));

        QVERIFY(m_services->moveItem(3, 0));
        QCOMPARE(m_services->selectedIndex(), 3);   // Blank, pushed down by the move
        QVERIFY(m_services->moveItem(3, 1));
        QCOMPARE(m_services->selectedIndex(), 1);   // the moved item stays selected
        QVERIFY(!m_services->moveItem(0, 4));
        QVERIFY(m_services->removeItem(2));         // Amazing Grace
        QCOMPARE(titles(m_services->items()), (QStringList{"Holy Spirit", "Blank", "Welcome"}));

        // Saved as it went, and the open service comes back with the library.
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(m_libraries))));
        QCOMPARE(m_services->openTitle(), QStringLiteral("Sunday"));
        QCOMPARE(titles(m_services->items()), (QStringList{"Holy Spirit", "Blank", "Welcome"}));
        const QVariantList items = m_services->items();
        QCOMPARE(items[0].toMap().value("kind").toString(), QStringLiteral("song"));
        QVERIFY(items[1].toMap().value("hasSlides").toBool());
        QVERIFY(!items[2].toMap().value("hasSlides").toBool());   // a header
    }

    void selectingAnItemShowsItsSlidesInTheGrid()
    {
        const QStringList songs = makeSongs({"Amazing Grace"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(songs[0]));
        QVERIFY(m_services->addText("Notices", "Coffee after the service"));
        QQuickWindow *window = loadOperatorWindow();
        auto *grid = window->findChild<QQuickItem *>("slideGrid");
        QTRY_COMPARE(grid->property("count").toInt(), 1);   // the text item, selected last
        click(window, item(window, "serviceItem0"));
        QCOMPARE(m_services->selectedIndex(), 0);
        QTRY_COMPARE(grid->property("count").toInt(), 3);
        QCOMPARE(m_services->selectedSongId(), songs[0]);
    }

    // The issue's "done when": a hand-built service of 4 songs, run start to finish from the
    // keyboard. Each song has 3 slides; a header sits between the second and third.
    void aServiceOfFourSongsRunsFromTheKeyboard()
    {
        QQuickWindow *window = serviceOperatorWindow();
        QTest::keyClick(window, Qt::Key_Return);   // the selected item: the first song
        QCOMPARE(m_live->itemIndex(), 0);
        QCOMPARE(m_live->slideIndex(), 0);
        QList<std::pair<int, int>> seen{{0, 0}};
        for (int i = 0; i < 11; ++i) {
            QTest::keyClick(window, i % 2 ? Qt::Key_Space : Qt::Key_Right);
            seen << std::pair{m_live->itemIndex(), m_live->slideIndex()};
        }
        const QList<std::pair<int, int>> expected{{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2},
                                                  {3, 0}, {3, 1}, {3, 2}, {4, 0}, {4, 1}, {4, 2}};
        QCOMPARE(seen, expected);   // the header (item 2) skipped
        QCOMPARE(m_live->songTitle(), QStringLiteral("Song 4"));
        QTest::keyClick(window, Qt::Key_Right);   // the end of the service: stays
        QCOMPARE(m_live->itemIndex(), 4);
        QCOMPARE(m_live->slideIndex(), 2);
        // The grid followed the live item.
        QCOMPARE(m_services->selectedIndex(), 4);
        QTRY_VERIFY(tile(window, 2)->property("isCurrent").toBool());

        for (int i = 0; i < 3; ++i)
            QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(m_live->itemIndex(), 3);
        QCOMPARE(m_live->slideIndex(), 2);   // Previous lands on the song before's last slide

        if (const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
            QTest::qWait(300);
            window->grabWindow().save(dir + "/operator-service.png");
        }
    }

    void tileClicksOnAServiceItemAreCommands()
    {
        QQuickWindow *window = serviceOperatorWindow();
        click(window, item(window, "serviceItem1"));
        click(window, tile(window, 2));
        QCOMPARE(m_live->itemIndex(), 1);
        QCOMPARE(m_live->slideIndex(), 2);
        QTRY_VERIFY(tile(window, 2)->property("isCurrent").toBool());
        click(window, tile(window, 0));   // the live item: goto_slide
        QCOMPARE(m_live->slideIndex(), 0);
        QCOMPARE(m_live->itemIndex(), 1);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(m_live->itemIndex(), 0);
    }

    void itemsReorderByKeysAndByDrag()
    {
        QQuickWindow *window = serviceOperatorWindow();
        click(window, item(window, "serviceItem0"));
        QTest::keyClick(window, Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(titles(m_services->items()), (QStringList{"Song 2", "Song 1", "Offering", "Song 3", "Song 4"}));
        QCOMPARE(m_services->selectedIndex(), 1);
        QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(window, Qt::Key_Up, Qt::AltModifier);   // already first
        QCOMPARE(titles(m_services->items()), (QStringList{"Song 1", "Song 2", "Offering", "Song 3", "Song 4"}));
        QCOMPARE(m_live->itemIndex(), -1);   // nothing went live

        // Drag Song 4 up onto Song 2's row.
        QQuickItem *from = item(window, "serviceItem4");
        QQuickItem *onto = item(window, "serviceItem1");
        const QPoint start = from->mapToScene(QPointF(from->width() / 2, from->height() / 2)).toPoint();
        const QPoint end = onto->mapToScene(QPointF(onto->width() / 2, onto->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(window, start + (end - start) * step / 10);
        QTest::mouseRelease(window, Qt::LeftButton, {}, end);
        QCOMPARE(titles(m_services->items()), (QStringList{"Song 1", "Song 4", "Song 2", "Offering", "Song 3"}));
    }

    void importsServiceExportsAndOpensThem()
    {
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(FIXTURES_DIR "/songs")));
        QVERIFY(waitForImport());
        const QVariantList songs = m_songs->songs();
        Service s;
        s.id = QUuid::createUuid();
        s.title = "Imported";
        s.items = {ServiceItem{QUuid::createUuid(), ServiceItem::Kind::Song, "Amazing Grace",
                               QUuid::fromString(songs[0].toMap().value("id").toString()), {}, {}, {}, {}, {}, {}, {}},
                   ServiceItem{QUuid::createUuid(), ServiceItem::Kind::Song, "Gone", QUuid::createUuid(), {}, {}, {}, {}, {}, {}, {}}};
        QDir folder(m_dir.filePath("exports"));
        QVERIFY(folder.mkpath("."));
        QFile f(folder.filePath("Imported.visualis-service.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(serviceToJson(s));
        f.close();
        QFile bad(folder.filePath("Broken.visualis-service.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{");
        bad.close();

        m_services->importServices(QUrl::fromLocalFile(folder.path()));
        QVERIFY(m_services->report().startsWith("Imported 1 services."));
        QVERIFY(m_services->report().contains("Broken"));
        QCOMPARE(m_services->openTitle(), QStringLiteral("Imported"));
        const QVariantList items = m_services->items();
        QCOMPARE(items[0].toMap().value("kind").toString(), QStringLiteral("song"));
        QCOMPARE(items[1].toMap().value("kind").toString(), QStringLiteral("placeholder"));   // not in this library
        m_services->importServices(QUrl::fromLocalFile(folder.path()));
        QVERIFY(m_services->report().contains("already in the library"));
        QCOMPARE(m_services->services().size(), 1);
    }

    // #69: imports run on a worker; the operator window keeps going.
    void aBigImportKeepsTheEventLoopRunningAndMatchesTheSynchronousOne()
    {
        const QString folder = writeSongFiles(1000);
        int ticks = 0;
        qint64 longestGap = 0;
        QElapsedTimer sinceTick;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, this, [&] {
            longestGap = qMax(longestGap, sinceTick.restart());
            ++ticks;
        });
        QElapsedTimer took;
        took.start();
        sinceTick.start();
        timer.start();
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QVERIFY(m_songs->importing());
        QVERIFY(waitForImport());
        timer.stop();
        longestGap = qMax(longestGap, sinceTick.elapsed());
        qInfo() << "import took" << took.elapsed() << "ms;" << ticks << "ticks, longest gap" << longestGap << "ms";
        QVERIFY(!m_songs->importing());
        // On this thread the whole import would be one gap. A 10 ms timer late by more than a
        // few frames means the window and outputs would stutter.
        QVERIFY2(longestGap < kMaxStallMs, qPrintable(QString("the event loop stalled for %1 ms of a %2 ms import")
                                                           .arg(longestGap).arg(took.elapsed())));
        QVERIFY(ticks >= 5);

        // The same songs and report as importing on this thread.
        QTemporaryDir other;
        auto library = Library::open(other.path()).library;
        SongStore store(*library);
        const SongImportReport sync = importSongs(folder, store);
        QCOMPARE(m_songs->songs().size(), 1000);
        QStringList syncTitles;
        for (const SongSummary &s : store.list())
            syncTitles << s.title;
        QCOMPARE(titles(m_songs->songs()), syncTitles);
        QCOMPARE(m_songs->report(),
                 SongLibrary::importReport({{}, sync.imported, sync.skipped, sync.failed, sync.warnings}));
    }

    // #77: the operator's saves never wait on another connection's lock during an import,
    // because no other connection writes.
    void guiSavesKeepLandingDuringALongImport()
    {
        const QString folder = writeSongFiles(2000);
        qint64 longestGap = 0;
        QElapsedTimer sinceTick;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, this, [&] { longestGap = qMax(longestGap, sinceTick.restart()); });
        sinceTick.start();
        timer.start();
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));

        qint64 slowestSave = 0;
        int pasted = 0;
        bool onlyOneConnection = true;
        while (m_songs->importing()) {
            onlyOneConnection = onlyOneConnection && QSqlDatabase::connectionNames().size() == 1;
            QElapsedTimer save;
            save.start();
            QVERIFY(m_songs->createFromPaste(QStringLiteral("Pasted %1").arg(pasted++), kLyrics));
            slowestSave = qMax(slowestSave, save.elapsed());
            QTest::qWait(5);
        }
        timer.stop();
        longestGap = qMax(longestGap, sinceTick.elapsed());
        qInfo() << pasted << "saves during the import; slowest" << slowestSave << "ms; longest stall" << longestGap << "ms";
        QVERIFY(pasted >= 5);
        QVERIFY2(onlyOneConnection, "the import opened a database connection of its own");
        QVERIFY2(slowestSave < kMaxSaveMs, qPrintable(QString("a save took %1 ms").arg(slowestSave)));
        QVERIFY2(longestGap < kMaxStallMs, qPrintable(QString("the event loop stalled for %1 ms").arg(longestGap)));
        QCOMPARE(m_songs->songs().size(), 2000 + pasted);
        QVERIFY(m_songs->report().startsWith("Imported 2000 songs."));
    }

    void oneImportAtATimeAndTheLibraryStaysWritable()
    {
        const QString folder = writeSongFiles(200);
        QSignalSpy busy(m_songs, &SongLibrary::importingChanged);
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QVERIFY(!m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QVERIFY(!m_songs->importProPresenter(QUrl::fromLocalFile(folder)));
        QVERIFY(m_songs->importProgress().startsWith("Importing"));
        // A save on the operator's connection waits its turn instead of failing.
        QVERIFY(m_songs->createFromPaste("Pasted Mid-Import", kLyrics));
        QVERIFY(waitForImport());
        QCOMPARE(m_songs->songs().size(), 201);
        QVERIFY(m_songs->report().startsWith("Imported 200 songs."));
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));   // free again
        QVERIFY(waitForImport());
    }

    void openingAnotherLibraryStopsAnImportWithoutWaitingForIt()
    {
        const QString first = m_dir.filePath(QString::number(m_libraries));
        const QString folder = writeSongFiles(1000);
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QTRY_VERIFY(!m_songs->songs().isEmpty());   // under way: a batch has been saved
        QElapsedTimer took;
        took.start();
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(++m_libraries))));
        QVERIFY2(took.elapsed() < kMaxStallMs, qPrintable(QString("opening took %1 ms").arg(took.elapsed())));
        QVERIFY(m_songs->songs().isEmpty());   // the new library, at once
        QVERIFY(m_songs->importing());         // the old import is stopping...
        QCOMPARE(m_songs->importProgress(), QStringLiteral("Stopping the import…"));
        QVERIFY(!m_songs->importSongs(QUrl::fromLocalFile(folder)));

        QVERIFY(waitForImport());   // ...and says what it got through
        const QString report = m_songs->report();
        QVERIFY2(report.startsWith("The import was stopped after "), qPrintable(report));
        const int imported = QRegularExpression("Imported (\\d+) songs").match(report).captured(1).toInt();
        QVERIFY(imported > 0 && imported < 1000);
        QVERIFY(m_songs->songs().isEmpty());   // into the old library, not this one
        QVERIFY(m_songs->openLibrary(first));
        QCOMPARE(m_songs->songs().size(), imported);
    }

    void quittingMidImportWaitsOnlyBriefly()
    {
        const QString folder = writeSongFiles(1000);
        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QTRY_VERIFY(m_songs->importProgress().contains(" of 1000"));
        QElapsedTimer took;
        took.start();
        m_engine.reset();   // the singletons go with the engine, as at quit
        m_songs = nullptr;
        QVERIFY2(took.elapsed() < 3500, qPrintable(QString("quitting took %1 ms").arg(took.elapsed())));
        QTest::qWait(50);   // nothing from the stopped worker arrives at a deleted object
    }

    // #55: an imported service whose songs the library doesn't have yet. Returns its id.
    QString openImportedService(const QStringList &songTitles, bool autoLink = false)
    {
        PcoPlanItems plan;
        for (int i = 0; i < songTitles.size(); ++i) {
            PcoItem it;
            it.id = QString::number(i + 1);
            it.type = PcoItemType::Song;
            it.title = songTitles[i];
            it.position = i + 1;
            it.song = PcoSong{"p" + it.id, songTitles[i], {}, "Traditional", "Public Domain"};
            it.arrangement = PcoArrangement{"a" + it.id, "Default", {"Intro", "Verse 1", "Chorus", "Bridge 2"}, 0, {}, 0};
            plan.items << it;
        }
        PcoPlan p;
        p.id = "70001";
        Service s = serviceFromPlan({"900", "Sunday"}, p, plan, QDateTime::currentDateTimeUtc()).service;
        if (autoLink) {
            SongStore songs(*m_songs->library());
            linkSongs(s, songs);   // as PlanImporter does
        }
        if (!ServiceStore(*m_songs->library()).save(s).ok)
            qFatal("couldn't save the imported service");
        const QString id = s.id.toString(QUuid::WithoutBraces);
        m_services->openAdded(id);
        return id;
    }

    QVariantMap item(int index) const { return m_services->items().value(index).toMap(); }

    void anImportedSongIsResolvedByASuggestion()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));   // two: never picked for the operator
        openImportedService({"Amazing Grace"});
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("placeholder"));
        QVERIFY(item(0)["songPlaceholder"].toBool());
        const QVariantList suggestions = item(0)["suggestions"].toList();
        QCOMPARE(suggestions.size(), 2);

        const QString chosen = suggestions[1].toMap()["id"].toString();
        QVERIFY(m_services->linkSong(0, chosen));
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("song"));
        QVERIFY(!item(0)["songPlaceholder"].toBool());
        QVERIFY(item(0)["hasSlides"].toBool());
        QCOMPARE(m_songs->loadSong(QUuid::fromString(chosen))->pcoSongId, QStringLiteral("p1"));   // next week: at once
        QVERIFY(m_services->report().contains("Bridge 2"));   // the gap in the song is pointed out
        QCOMPARE(m_services->selectedIndex(), 0);
    }

    // Review of #85: a wrong link by title doesn't stick. Relinking moves the Planning
    // Center id, so next week links the right song by id instead of asking again.
    void relinkingMovesThePlanningCenterId()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        const QUuid wrong = QUuid::fromString(m_songs->selectedId());
        QVERIFY(m_songs->createFromPaste("Amazing Grace Hymn", kLyrics));
        const QUuid right = QUuid::fromString(m_songs->selectedId());

        openImportedService({"Amazing Grace"}, true);   // links to "Amazing Grace" by title
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("song"));
        QCOMPARE(m_songs->loadSong(wrong)->pcoSongId, QStringLiteral("p1"));

        QVERIFY(m_services->linkSong(0, right.toString(QUuid::WithoutBraces)));
        QVERIFY(m_songs->loadSong(wrong)->pcoSongId.isEmpty());
        QCOMPARE(m_songs->loadSong(right)->pcoSongId, QStringLiteral("p1"));

        openImportedService({"Amazing Grace"}, true);   // next week's plan
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("song"));
        m_services->select(0);
        QCOMPARE(m_services->selectedSongId(), right.toString(QUuid::WithoutBraces));   // by id, not "confirm"
    }

    void anImportedSongIsLinkedToTheLibrarysSelectedSong()
    {
        QVERIFY(m_songs->createFromPaste("Grace Hymn", kLyrics));
        const QString id = m_songs->selectedId();
        openImportedService({"Something Else Entirely"});
        QVERIFY(item(0)["suggestions"].toList().isEmpty());
        QVERIFY(m_services->linkSong(0, id));
        QCOMPARE(item(0)["title"].toString(), QStringLiteral("Grace Hymn"));
        // The link survives reopening the service.
        m_services->openService(m_services->openId());
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("song"));
    }

    void anImportedSongCanBeLeftBlank()
    {
        openImportedService({"Not In The Library", "Another"});
        QVERIFY(m_services->leaveBlank(1));
        QCOMPARE(item(1)["kind"].toString(), QStringLiteral("text"));
        QVERIFY(item(1)["hasSlides"].toBool());   // one blank slide to land on
        QVERIFY(!item(1)["songPlaceholder"].toBool());
        QCOMPARE(item(0)["kind"].toString(), QStringLiteral("placeholder"));
        QVERIFY(!m_services->linkSong(0, QUuid::createUuid().toString()));   // no such song
        QVERIFY(!m_services->linkSong(5, m_songs->selectedId()));
    }

    void aSongAddedAfterTheImportIsSuggested()
    {
        openImportedService({"Amazing Grace"});
        QVERIFY(item(0)["suggestions"].toList().isEmpty());
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));   // pasted in from the library pane
        QTRY_COMPARE(item(0)["suggestions"].toList().size(), 1);
        QVERIFY(m_services->linkSong(0, m_songs->selectedId()));
    }

    void theResolveButtonShowsForAnUnlinkedSong()
    {
        QQuickWindow *window = loadOperatorWindow();
        openImportedService({"Not In The Library"});
        m_services->addHeader("Worship");   // after the song, selected
        auto *resolve = findItem(window->contentItem(), "resolvePlaceholder");
        QVERIFY(resolve);
        QVERIFY(!resolve->isVisible());
        m_services->select(0);
        QTRY_VERIFY(resolve->isVisible());
        m_services->leaveBlank(0);
        QTRY_VERIFY(!resolve->isVisible());
    }

    void importReportNamesSkippedFilesByReason()
    {
        SongLibrary::ImportSummary s{"from ProPresenter", {"A", "B"},
                                     {{"x.pro", "not a song"}, {"y.pro", "too large to be a song"}, {"z.pro", "not a song"}},
                                     {{"bad.pro", "truncated"}}, {}};
        QCOMPARE(SongLibrary::importReport(s),
                 QStringLiteral("Imported 2 songs from ProPresenter.\n"
                                "Skipped 2: not a song:\n  x.pro\n  z.pro\n"
                                "Skipped 1: too large to be a song:\n  y.pro\n"
                                "Couldn't read bad.pro: truncated"));
        s.stopped = true;
        s.filesDone = 5;
        s.filesTotal = 9;
        QVERIFY(SongLibrary::importReport(s).startsWith("The import was stopped after 5 of 9 files.\nImported 2 songs"));
    }

private:
    bool waitForImport()
    {
        if (!m_songs->importing())
            return true;
        QSignalSpy finished(m_songs, &SongLibrary::importFinished);
        return finished.wait(30000);
    }

    // `count` synthetic songs as Visualis exports, in their own folder.
    QString writeSongFiles(int count)
    {
        const QString folder = m_dir.filePath(QStringLiteral("songs-%1").arg(++m_folders));
        QDir().mkpath(folder);
        for (int i = 0; i < count; ++i) {
            Song s;
            s.title = QStringLiteral("Synthetic %1").arg(i, 4, 10, QChar('0'));
            s.groups = {Group{QUuid::createUuid(), "Verse 1", "", {"line one\nline two", "line three"}},
                        Group{QUuid::createUuid(), "Chorus", "", {"chorus"}}};
            QFile f(QDir(folder).filePath(songFileName(s.title)));
            if (!f.open(QIODevice::WriteOnly))
                qFatal("can't write a song file");
            f.write(songToJson(s));
        }
        return folder;
    }

    QStringList makeSongs(const QStringList &names)
    {
        QStringList ids;
        for (const QString &name : names) {
            if (!m_songs->createFromPaste(name, kLyrics))
                qFatal("couldn't create a test song");
            ids << m_songs->selectedId();
        }
        return ids;
    }

    // Song 1, Song 2, an Offering header, Song 3, Song 4 (3 slides each), the first song
    // selected, nothing live, the window active.
    QQuickWindow *serviceOperatorWindow()
    {
        const QStringList songs = makeSongs({"Song 1", "Song 2", "Song 3", "Song 4"});
        if (!m_services->createService("Sunday"))
            qFatal("couldn't create the test service");
        for (int i = 0; i < 4; ++i) {
            m_services->addSong(songs[i]);
            if (i == 1)
                m_services->addHeader("Offering");
        }
        m_services->select(0);
        QQuickWindow *window = loadOperatorWindow();
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window))
            qFatal("operator window didn't activate");
        return window;
    }

    static QQuickItem *item(QQuickWindow *window, const QString &name)
    {
        for (int waited = 0; waited < 2000; waited += 20) {
            if (QQuickItem *found = findItem(window->contentItem(), name))
                return found;
            QTest::qWait(20);
        }
        qFatal("no item %s", qPrintable(name));
    }

    // The operator window with the 3-slide song live at its first slide, focus on nothing.
    QQuickWindow *liveOperatorWindow()
    {
        if (!m_songs->createFromPaste("Amazing Grace", kLyrics))
            qFatal("couldn't create the test song");
        QQuickWindow *window = loadOperatorWindow();
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window))
            qFatal("operator window didn't activate");
        m_songs->goLive(0);
        return window;
    }

    QQuickWindow *loadOperatorWindow()
    {
        m_engine->loadFromModule("Visualis.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        if (!window || !QTest::qWaitForWindowExposed(window))
            qFatal("operator window didn't open");
        return window;
    }

    static QQuickItem *tile(QQuickWindow *window, int index)
    {
        // The grid creates its tiles after the model changes. Delegates have no QObject
        // parent, so search the visual item tree rather than findChild.
        const QString name = QStringLiteral("slideTile%1").arg(index);
        for (int waited = 0; waited < 2000; waited += 20) {
            if (QQuickItem *found = findItem(window->contentItem(), name))
                return found;
            QTest::qWait(20);
        }
        return nullptr;
    }

    static QQuickItem *findItem(QQuickItem *item, const QString &name)
    {
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    static void click(QQuickWindow *window, QQuickItem *item)
    {
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
    }

    QTemporaryDir m_dir;
    static constexpr qint64 kMaxStallMs = 100;
    // An idle save is ~5 ms, nearly all disk sync; one behind another connection's lock took
    // up to 205 ms before #77.
    static constexpr qint64 kMaxSaveMs = 50;
    int m_libraries = 0;
    int m_folders = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    SongLibrary *m_songs = nullptr;
    LiveState *m_live = nullptr;
    ServiceList *m_services = nullptr;
};

QTEST_MAIN(OperatorTest)
#include "tst_operator.moc"
