#include "background/backgroundplayer.h"
#include "library/medialibrary.h"
#include "media/mediaresolve.h"
#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "library/templatelibrary.h"
#include "library/library.h"
#include "pco/planimport.h"
#include "pco/songlink.h"
#include "service/servicestore.h"
#include "service/servicejson.h"
#include "song/songjson.h"
#include "song/songstore.h"
#include "live/livestate.h"
#include "propresenterbytes.h"
#include "song/songlimits.h"

#include <QFont>
#include <QQmlApplicationEngine>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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

    // #97: deleting media a service shows names the service before anything is removed.
    void deletingMediaAServiceUsesAsksFirst()
    {
        auto *media = m_engine->singletonInstance<MediaLibrary *>("Visualis.Ui", "MediaLibrary");
        QVERIFY(media);
        QTemporaryDir in;
        QImage image(16, 9, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(in.filePath("Sunrise.png")));
        QSignalSpy finished(media, &MediaLibrary::importFinished);
        QVERIFY(media->importFiles({QUrl::fromLocalFile(in.filePath("Sunrise.png"))}));
        QVERIFY(finished.wait(10'000));
        QCOMPARE(media->assets().size(), 1);
        const QString id = media->assets()[0].toMap().value("id").toString();

        Service s;
        s.title = "Easter Sunday";
        ServiceItem item;
        item.kind = ServiceItem::Kind::Media;
        item.title = "Sunrise";
        item.ref = QUuid::fromString(id);
        s.items << item;
        QVERIFY(ServiceStore(*m_songs->library()).save(s).ok);

        QQuickWindow *window = loadOperatorWindow();
        QObject *dialog = window->findChild<QObject *>("mediaDeleteDialog");
        QVERIFY(dialog);
        dialog->setProperty("assetId", id);
        dialog->setProperty("assetName", "Sunrise.png");
        QVERIFY(QMetaObject::invokeMethod(dialog, "ask"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        const QString text = window->findChild<QObject *>("mediaDeleteText")->property("text").toString();
        QVERIFY2(text.contains("Easter Sunday"), qPrintable(text));
        QCOMPARE(media->assets().size(), 1);   // nothing gone while it asks

        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(media->assets().isEmpty());
    }

    // #98: the background is picked from the media library and plays from the library's file.
    // One whose file has gone is refused with a warning, and what was showing stays.
    void aBackgroundIsPickedFromTheMediaLibrary()
    {
        auto *media = m_engine->singletonInstance<MediaLibrary *>("Visualis.Ui", "MediaLibrary");
        auto *player = m_engine->singletonInstance<BackgroundPlayer *>("Visualis.Ui", "BackgroundPlayer");
        QVERIFY(media && player);
        QTemporaryDir in;
        for (const auto &[name, color] : {std::pair{"Sunrise.png", Qt::red}, std::pair{"Cross.png", Qt::blue}}) {
            QImage image(64, 36, QImage::Format_RGB32);
            image.fill(color);
            QVERIFY(image.save(in.filePath(name)));
        }
        QSignalSpy finished(media, &MediaLibrary::importFinished);
        QVERIFY(media->importFiles({QUrl::fromLocalFile(in.path())}));
        QVERIFY(finished.wait(10'000));
        QCOMPARE(media->assets().size(), 2);
        const QString cross = media->assets()[0].toMap().value("id").toString();
        const QString sunrise = media->assets()[1].toMap().value("id").toString();

        QQuickWindow *window = loadOperatorWindow();
        QObject *picker = window->findChild<QObject *>("backgroundPicker");
        QVERIFY(picker);
        QVERIFY(QMetaObject::invokeMethod(picker, "choose", Q_ARG(QVariant, sunrise)));
        QCOMPARE(m_live->backgroundId(), sunrise);
        QTRY_COMPARE_WITH_TIMEOUT(player->showing(), QUuid::fromString(sunrise), 5000);
        QCOMPARE(player->status(), QStringLiteral("Showing a still"));

        // Its file goes missing from the media folder: picking it warns and keeps Sunrise.
        QVERIFY(QFile::remove(checkMedia(media->snapshot(), QUuid::fromString(cross)).path));
        QVERIFY(QMetaObject::invokeMethod(picker, "choose", Q_ARG(QVariant, cross)));
        QCOMPARE(m_live->backgroundId(), sunrise);
        QCOMPARE(player->showing(), QUuid::fromString(sunrise));
        auto *alert = window->findChild<QQuickItem *>("backgroundAlert");
        QTRY_VERIFY(alert->isVisible());
        QVERIFY2(alert->property("text").toString().contains("Cross.png"), qPrintable(alert->property("text").toString()));
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

    // #93: editing a template in the library shows at once in the open service and on what's
    // live, at the same slide; deleting it falls back to the built-in with a warning.
    void aTemplateEditReachesTheServiceAndWhatsLive()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        QVERIFY(templates);
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Ours";
        t.regions[0].style.fontFamily = "Avenir";
        QVERIFY(templates->save(t).ok);
        QVERIFY(templates->templates().contains(t.id));

        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        Song song = *m_songs->loadSong(QUuid::fromString(ids[0]));
        song.defaultTemplate = t.id;
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_live->goToItem(0, 1));
        auto mainFont = [this] { return m_live->content().regions.value("main").style.fontFamily; };
        QCOMPARE(mainFont(), QStringLiteral("Avenir"));

        t.regions[0].style.fontFamily = "Georgia";
        QVERIFY(templates->save(t).ok);
        QCOMPARE(mainFont(), QStringLiteral("Georgia"));
        QCOMPARE(m_live->itemIndex(), 0);
        QCOMPARE(m_live->slideIndex(), 1);
        QVERIFY(m_services->items()[0].toMap()["warnings"].toString().isEmpty());

        // A song taken live from the library follows too.
        m_songs->select(ids[0]);
        QVERIFY(m_songs->goLive(2));
        t.regions[0].style.fontFamily = "Menlo";
        QVERIFY(templates->save(t).ok);
        QCOMPARE(mainFont(), QStringLiteral("Menlo"));
        QCOMPARE(m_live->slideIndex(), 2);

        QVERIFY(templates->remove(t.id).ok);   // gone: never a blank output
        QCOMPARE(mainFont(), builtInLyricsTemplate().regions[0].style.fontFamily);
        QVERIFY(!m_services->items()[0].toMap()["warnings"].toString().isEmpty());
    }

    // #93 with #44: a template change re-renders a library song as it went live, so an edit
    // saved since still waits for the next go-live.
    void aTemplateChangeDoesntBringInALiveSongsUnshownEdit()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        const QStringList ids = makeSongs({"Amazing Grace"});
        m_songs->select(ids[0]);
        QVERIFY(m_songs->goLive(0));
        const QString shown = m_live->currentText();

        Song song = *m_songs->loadSong(QUuid::fromString(ids[0]));
        song.groups[0].slides[0] = "Edited since";
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Any";
        QVERIFY(templates->save(t).ok);

        QCOMPARE(m_live->currentText(), shown);
    }

    // #93 review: deleting the song that's live in a service never blanks the output. The
    // copy on screen stays and the operator is told.
    void deletingTheLiveSongKeepsItOnScreen()
    {
        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_services->addSong(ids[1]));
        QQuickWindow *window = loadOperatorWindow();
        QVERIFY(m_live->goToItem(0, 1));
        const QString shown = m_live->currentText();

        QVERIFY(m_songs->removeSong(ids[0]));
        QCOMPARE(m_live->currentText(), shown);
        QCOMPARE(m_live->slideIndex(), 1);
        QQuickItem *banner = findItem(window->contentItem(), "liveHeldBack");
        QVERIFY(banner);
        QTRY_VERIFY(banner->isVisible());

        QVERIFY(m_live->goToItem(1, 0));
        QTRY_VERIFY(!banner->isVisible());
    }

    // #93: a stored template that can't be read is left out and logged; what uses it shows
    // in the built-in, never blank.
    void anUnreadableTemplateFallsBackToTheBuiltIn()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Damaged";
        t.regions[0].style.fontFamily = "Avenir";
        QVERIFY(templates->save(t).ok);
        const QStringList ids = makeSongs({"Amazing Grace"});
        Song song = *m_songs->loadSong(QUuid::fromString(ids[0]));
        song.defaultTemplate = t.id;
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));

        {
            QSqlQuery q(m_songs->library()->database());
            q.prepare("UPDATE template SET body = '{not json' WHERE id = ?");
            q.addBindValue(t.id.toString(QUuid::WithoutBraces));
            QVERIFY2(q.exec() && q.numRowsAffected() == 1, qPrintable(q.lastError().text()));
        }
        const QString folder = m_songs->folder();   // a copy: openLibrary closes the old library first
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("template \"Damaged\":"));
        QVERIFY(m_songs->openLibrary(folder));   // re-read, as at startup

        QVERIFY(!templates->templates().contains(t.id));
        QVERIFY(m_services->createService("Sunday"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("built-in"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_live->goToItem(0, 0));
        QCOMPARE(m_live->content().regions.value("main").style.fontFamily,
                 builtInLyricsTemplate().regions[0].style.fontFamily);
        QVERIFY(m_services->items()[0].toMap()["warnings"].toString().contains("built-in"));
    }

    // #93: the grid's tiles draw the slide's content, so a preview matches the output.
    void previewTilesRenderThroughTheSlidesTemplate()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Ours";
        t.regions[0].style.fontFamily = "Georgia";
        t.regions[0].style.frame = QRectF(0.1, 0.6, 0.8, 0.3);
        QVERIFY(templates->save(t).ok);
        const QStringList ids = makeSongs({"Amazing Grace"});
        Song song = *m_songs->loadSong(QUuid::fromString(ids[0]));
        song.defaultTemplate = t.id;
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));
        m_songs->select(ids[0]);

        QQuickWindow *window = loadOperatorWindow();
        QQuickItem *preview = tile(window, 1);
        QVERIFY(preview);
        QQuickItem *text = findItem(preview, "lyricsA");
        QVERIFY(text);
        QTRY_VERIFY(!text->property("text").toString().isEmpty());
        QCOMPARE(text->property("font").value<QFont>().family(), QStringLiteral("Georgia"));
        // The frame sits where the template puts it, at the tile's scale.
        auto *lyrics = text->parentItem()->parentItem();   // the frame's AudienceLyrics
        QCOMPARE(text->parentItem()->y(), lyrics->height() * 0.6);

        QVERIFY(m_songs->goLive(1));   // the output gets the same content
        QCOMPARE(m_live->content(), m_songs->selectedSlides()[1].toMap()["content"].value<Content>());
    }

    // #93: an item that shows, but not as asked, is marked in the service list.
    void anItemWhoseTemplateIsMissingIsMarked()
    {
        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        Song song = *m_songs->loadSong(QUuid::fromString(ids[0]));
        song.defaultTemplate = QUuid::createUuid();   // a template that's gone
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_services->addSong(ids[1]));
        QQuickWindow *window = loadOperatorWindow();
        QQuickItem *marked = findItem(window->contentItem(), "itemWarning0");
        QVERIFY(marked);
        QTRY_VERIFY(marked->isVisible());
        QVERIFY(m_services->items()[0].toMap()["warnings"].toString().contains("built-in"));
        QVERIFY(!findItem(window->contentItem(), "itemWarning1")->isVisible());
    }

    // #95: a service item can use another template for this service only.
    void anItemsTemplateIsChosenFromTheServiceList()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Christmas";
        t.regions[0].style.color = "#ff0000";
        QVERIFY(templates->save(t).ok);
        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_services->addSong(ids[1]));
        const QString tid = t.id.toString(QUuid::WithoutBraces);

        QVERIFY(m_services->setItemTemplate(0, tid));
        QCOMPARE(m_services->items()[0].toMap()["templateId"].toString(), tid);
        QCOMPARE(m_services->items()[1].toMap()["templateId"].toString(), QString());
        QVERIFY(m_live->goToItem(0, 1));
        QCOMPARE(m_live->content().regions.value("main").style.color, QStringLiteral("#ff0000"));
        // Saved with the service; the song itself keeps its own template.
        const Service stored = *ServiceStore(*m_songs->library()).load(QUuid::fromString(m_services->openId()));
        QCOMPARE(overrideTemplateId(stored.items[0].overrides), t.id);
        QVERIFY(m_songs->loadSong(QUuid::fromString(ids[0]))->defaultTemplate.isNull());

        QVERIFY(m_services->setItemTemplate(0, ""));   // back to the song's
        QCOMPARE(m_services->items()[0].toMap()["templateId"].toString(), QString());
        QCOMPARE(m_live->content().regions.value("main").style.color, builtInLyricsTemplate().regions[0].style.color);
        QVERIFY(!m_services->setItemTemplate(5, tid));
    }

    // #99: media goes into a service where it's dropped, or after the selection.
    void mediaIsAddedToAServiceWhereItsDropped()
    {
        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_services->addSong(ids[1]));
        const QUuid asset = QUuid::createUuid();
        QVERIFY(m_services->addMedia(asset.toString(QUuid::WithoutBraces), "Countdown", 1));
        QCOMPARE(titles(m_services->items()), (QStringList{"Amazing Grace", "Countdown", "Holy Spirit"}));
        QCOMPARE(m_services->selectedIndex(), 1);
        QVERIFY(m_services->addMedia(asset.toString(QUuid::WithoutBraces), "Announcements", -1));
        QCOMPARE(titles(m_services->items()), (QStringList{"Amazing Grace", "Countdown", "Announcements", "Holy Spirit"}));
        QVERIFY(!m_services->addMedia("", "Nothing", -1));
        QVERIFY(!m_services->addMedia(asset.toString(QUuid::WithoutBraces), "Too far", 9));

        const Service stored = *ServiceStore(*m_songs->library()).load(QUuid::fromString(m_services->openId()));
        QCOMPARE(stored.items[1].kind, ServiceItem::Kind::Media);
        QCOMPARE(stored.items[1].ref, asset);
        QCOMPARE(m_services->items()[1].toMap()["kind"].toString(), QStringLiteral("media"));
        QVERIFY(m_services->items()[1].toMap()["hasSlides"].toBool());
        // A media item is its asset: an override of it wouldn't show.
        QVERIFY(!m_services->items()[1].toMap()["canHaveBackground"].toBool());
        QVERIFY(m_services->items()[0].toMap()["canHaveBackground"].toBool());
    }

    // #99: an item's background and the service's default are set from the service list,
    // saved with the service, and reach LiveState.
    void backgroundsAreSetFromTheServiceList()
    {
        const QStringList ids = makeSongs({"Amazing Grace", "Holy Spirit"});
        QVERIFY(m_services->createService("Sunday"));
        QVERIFY(m_services->addSong(ids[0]));
        QVERIFY(m_services->addSong(ids[1]));
        // Real files: LiveState checks each against the media library before it goes up.
        auto *media = m_engine->singletonInstance<MediaLibrary *>("Visualis.Ui", "MediaLibrary");
        QTemporaryDir in;
        for (const auto &[name, color] : {std::pair{"Cross.png", Qt::blue}, std::pair{"Sunrise.png", Qt::red}}) {
            QImage image(16, 9, QImage::Format_RGB32);
            image.fill(color);
            QVERIFY(image.save(in.filePath(name)));
        }
        QSignalSpy finished(media, &MediaLibrary::importFinished);
        QVERIFY(media->importFiles({QUrl::fromLocalFile(in.path())}));
        QVERIFY(finished.wait(10'000));
        QCOMPARE(media->assets().size(), 2);
        const QString own = media->assets()[0].toMap().value("id").toString();
        const QString fallback = media->assets()[1].toMap().value("id").toString();

        QVERIFY(m_services->setItemBackground(0, own));
        QVERIFY(m_services->setServiceBackground(fallback));
        QCOMPARE(m_services->items()[0].toMap()["backgroundId"].toString(), own);
        QCOMPARE(m_services->items()[1].toMap()["backgroundId"].toString(), QString());
        QCOMPARE(m_services->serviceBackgroundId(), fallback);
        QVERIFY(!m_services->setItemBackground(5, own));

        const Service stored = *ServiceStore(*m_songs->library()).load(QUuid::fromString(m_services->openId()));
        QCOMPARE(overrideBackgroundId(stored.items[0].overrides), QUuid::fromString(own));
        QCOMPARE(stored.defaultBackground, QUuid::fromString(fallback));

        QVERIFY(m_live->goToItem(0));
        QCOMPARE(m_live->backgroundId(), own);
        QVERIFY(m_live->pickBackground(""));
        QVERIFY(m_live->goToItem(1));
        QCOMPARE(m_live->backgroundId(), fallback);   // the default fills the empty layer

        QVERIFY(m_services->setItemBackground(0, ""));
        QVERIFY(m_services->setServiceBackground(""));
        const Service cleared = *ServiceStore(*m_songs->library()).load(QUuid::fromString(m_services->openId()));
        QVERIFY(overrideBackgroundId(cleared.items[0].overrides).isNull());
        QVERIFY(cleared.defaultBackground.isNull());
        QCOMPARE(m_services->serviceBackgroundId(), QString());
    }

    // #99: a media tile dragged from the media pane goes into the service where it's dropped.
    void aMediaTileIsDraggedIntoTheService()
    {
        auto *media = m_engine->singletonInstance<MediaLibrary *>("Visualis.Ui", "MediaLibrary");
        QTemporaryDir in;
        QImage image(16, 9, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(in.filePath("Countdown.png")));
        QSignalSpy finished(media, &MediaLibrary::importFinished);
        QVERIFY(media->importFiles({QUrl::fromLocalFile(in.filePath("Countdown.png"))}));
        QVERIFY(finished.wait(10'000));
        const QString asset = media->assets()[0].toMap().value("id").toString();

        QQuickWindow *window = serviceOperatorWindow();
        click(window, item(window, "mediaTab"));
        QQuickItem *from = item(window, "mediaTile0");
        QQuickItem *onto = item(window, "serviceItem1");
        const QPoint start = from->mapToScene(QPointF(from->width() / 2, from->height() / 2)).toPoint();
        const QPoint end = onto->mapToScene(QPointF(onto->width() / 2, onto->height() - 4)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 20; ++step)
            QTest::mouseMove(window, start + (end - start) * step / 20);
        QTest::mouseRelease(window, Qt::LeftButton, {}, end);
        QTRY_COMPARE(titles(m_services->items()), (QStringList{"Song 1", "Song 2", "Countdown", "Offering", "Song 3", "Song 4"}));
        QCOMPARE(m_services->items()[2].toMap()["kind"].toString(), QStringLiteral("media"));
        QVERIFY(m_live->goToItem(2));
        QCOMPARE(m_live->backgroundId(), asset);
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
        // Each saved batch signals progress; record how many batches were in at each paste.
        int batches = 0;
        connect(m_songs, &SongLibrary::importProgressChanged, this, [&] { ++batches; });
        QSet<int> batchesAtEachPaste;
        const QDeadlineTimer deadline(60000);
        while (m_songs->importing()) {
            QVERIFY2(!deadline.hasExpired(), "the import never finished");
            onlyOneConnection = onlyOneConnection && QSqlDatabase::connectionNames().size() == 1;
            batchesAtEachPaste.insert(batches);
            QElapsedTimer save;
            save.start();
            QVERIFY(m_songs->createFromPaste(QStringLiteral("Pasted %1").arg(pasted++), kLyrics));
            slowestSave = qMax(slowestSave, save.elapsed());
            QTest::qWait(5);
        }
        timer.stop();
        longestGap = qMax(longestGap, sinceTick.elapsed());
        qInfo() << pasted << "saves during the import; slowest" << slowestSave << "ms; longest stall" << longestGap << "ms";
        QVERIFY2(onlyOneConnection, "the import opened a database connection of its own");
        // The operator's saves landed between the import's batches, not before or after it.
        batchesAtEachPaste.remove(0);
        QVERIFY2(batchesAtEachPaste.size() >= 3, qPrintable(QString("saves landed between only %1 of the import's batches")
                                                               .arg(batchesAtEachPaste.size())));
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

    // #77 review: when a batch's commit really fails, the import reports its songs failed,
    // keeps none of them, and leaves the library writable.
    void aBatchThatFailsToCommitIsReportedAndKeepsNothing()
    {
        const QString folder = writeSongFiles(3);
        Song breaks;
        breaks.title = "Breaks the Commit";
        breaks.groups = {Group{QUuid::createUuid(), "Breaks the commit", "", {"line"}}};
        QFile f(QDir(folder).filePath(songFileName(breaks.title)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(songToJson(breaks));
        f.close();
        // A dangling default arrangement: the foreign key is deferred, so the save goes through
        // and the commit fails, as a full disk or I/O error would.
        QCOMPARE(QSqlDatabase::connectionNames().size(), 1);
        QSqlQuery trigger(QSqlDatabase::database(QSqlDatabase::connectionNames().first()));
        QVERIFY2(trigger.exec("CREATE TEMP TRIGGER breaks_commit AFTER INSERT ON song_group "
                              "WHEN NEW.label = 'Breaks the commit' BEGIN "
                              "UPDATE song SET default_arrangement = 'missing' WHERE id = NEW.song_id; END"),
                 qPrintable(trigger.lastError().text()));

        QVERIFY(m_songs->importSongs(QUrl::fromLocalFile(folder)));
        QVERIFY(waitForImport());
        const QString report = m_songs->report();
        QVERIFY2(report.contains(QStringLiteral("Couldn't read %1: ").arg(QFileInfo(f).fileName())), qPrintable(report));
        QVERIFY2(report.contains("FOREIGN KEY"), qPrintable(report));
        const QStringList kept = titles(m_songs->songs());
        QVERIFY(!kept.contains("Breaks the Commit"));
        // Whatever shared the failed batch is reported failed too; the rest is in and says so.
        QVERIFY2(report.startsWith(QStringLiteral("Imported %1 songs.").arg(kept.size())), qPrintable(report));
        QCOMPARE(report.count("Couldn't read "), 4 - kept.size());
        QVERIFY(m_songs->createFromPaste("After the Failure", kLyrics));
        QVERIFY(titles(m_songs->songs()).contains("After the Failure"));
    }

    // #83: a paste past a limit is refused, and the pane says why.
    void aPastePastALimitIsRefusedWithTheReason()
    {
        const QString line(songlimits::kCharactersPerSlide + 1, QChar('a'));
        QVERIFY(!m_songs->createFromPaste("Too Long", QStringLiteral("Verse 1\n") + line));
        QVERIFY2(m_songs->report().contains(
                     "too many characters in a slide of \"Verse 1\": 10,001; the limit is 10,000"),
                 qPrintable(m_songs->report()));
        QVERIFY(m_songs->songs().isEmpty());

        // In the window, the dialog stays open with the reason and what was typed.
        QQuickWindow *window = loadOperatorWindow();
        auto *dialog = window->findChild<QObject *>("libraryPasteDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialog->findChild<QObject *>("pasteTitle")->setProperty("text", "Too Long");
        dialog->findChild<QObject *>("pasteLyrics")->setProperty("text", QStringLiteral("Verse 1\n") + line);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->findChild<QObject *>("pasteProblem")->property("text").toString().contains("too many characters"));
        QCOMPARE(dialog->findChild<QObject *>("pasteLyrics")->property("text").toString().size(), line.size() + 8);
        QVERIFY(m_songs->songs().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));   // Cancel closes it for good
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTest::qWait(300);
        QVERIFY(!dialog->property("visible").toBool());
    }

    // #83: an oversized file in a GUI import is skipped by the worker with its reason and
    // warnings; the valid one beside it imports.
    void anOversizedFileInAnImportIsSkippedAndTheRestImports()
    {
        const QString folder = m_dir.filePath(QStringLiteral("pro-%1").arg(++m_folders));
        QDir().mkpath(folder);
        QByteArray huge = pb::bytes(11, pb::arrangement("a", "A", {"g0", "missing-group"}));   // a warning
        for (int i = 0; i <= songlimits::kGroups; ++i) {
            huge += pb::bytes(12, pb::cueGroup(QString("g%1").arg(i), "Verse", {}, {QString("c%1").arg(i)}));
            huge += pb::bytes(13, pb::cue(QString("c%1").arg(i), "Sing"));
        }
        QByteArray valid = pb::bytes(12, pb::cueGroup("g", "Chorus", {}, {"c"}));
        valid += pb::bytes(13, pb::cue("c", "Sing"));
        for (const auto &[name, bytes] : {std::pair{"Huge.pro", huge}, std::pair{"Valid.pro", valid}}) {
            QFile f(QDir(folder).filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        }

        QVERIFY(m_songs->importProPresenter(QUrl::fromLocalFile(folder)));
        QVERIFY(waitForImport());
        const QString report = m_songs->report();
        QVERIFY2(report.startsWith("Imported 1 songs from ProPresenter.\n"
                                   "Skipped 1: too many groups: 501; the limit is 500:\n  Huge.pro\n"),
                 qPrintable(report));
        QVERIFY2(report.contains("\nHuge.pro: "), qPrintable(report));   // the parser's warning
        QCOMPARE(titles(m_songs->songs()), QStringList{"Valid"});
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
            for (Song song : linkSongs(s, songs).songs)   // as PlanImporter does, with the service
                songs.save(song);
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

    // PR #123 review: linking saves the song, and an oversized style stored on it stays.
    void linkingKeepsAStoredOversizedStyle()
    {
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        const QString id = m_songs->selectedId();
        const Song song = *m_songs->loadSong(QUuid::fromString(id));
        QSqlQuery q(m_songs->library()->database());
        q.prepare("UPDATE song_group SET slide_overrides = ? WHERE id = ?");
        q.addBindValue(QStringLiteral("[{\"later\":\"%1\"}]").arg(QString(20 * 1024, 'x')));
        q.addBindValue(song.groups[1].id.toString(QUuid::WithoutBraces));
        QVERIFY(q.exec());
        const QJsonObject big = m_songs->loadSong(song.id)->groups[1].slideOverride(0);
        QVERIFY(!big.isEmpty());

        openImportedService({"Amazing Grace"});
        QVERIFY(m_services->linkSong(0, id));
        QCOMPARE(m_songs->loadSong(song.id)->pcoSongId, QStringLiteral("p1"));   // it saved
        QCOMPARE(m_songs->loadSong(song.id)->groups[1].slideOverride(0), big);
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

    // #56: an item Planning Center took out stays, marked, and the service can be updated.
    void anItemRemovedInPlanningCenterIsMarked()
    {
        QQuickWindow *window = loadOperatorWindow();
        const QString id = openImportedService({"Example Song", "Gone Song"});
        ServiceStore store(*m_songs->library());
        Service s = *store.load(QUuid::fromString(id));
        s.items[1].pcoRemoved = true;
        QVERIFY(store.save(s).ok);
        m_services->openAdded(id);
        QVERIFY(m_services->openImported());
        QVERIFY(item(1)["pcoRemoved"].toBool());

        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(window->contentItem(), "serviceItem1")));
        const auto marked = [](QQuickItem *r) {
            for (QQuickItem *c : r->childItems()) {
                if (c->property("text").toString().endsWith("(removed in Planning Center)")
                    && c->property("font").value<QFont>().strikeOut())
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(marked(row));
        auto *update = window->findChild<QObject *>("reimportService");
        QVERIFY(update);
        QVERIFY(update->property("enabled").toBool());

        QVERIFY(m_services->createService("Local only"));
        QVERIFY(!m_services->openImported());
        QTRY_VERIFY(!update->property("enabled").toBool());
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
        s.filesDone = 0;
        s.filesTotal = -1;   // stopped while the folder was still being listed
        QVERIFY(SongLibrary::importReport(s).startsWith("The import was stopped before its files were counted.\n"));
    }

private:
    bool waitForImport()
    {
        if (!m_songs->importing())
            return true;
        QSignalSpy finished(m_songs, &SongLibrary::importFinished);
        return finished.wait(60000);
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
    // Wall-clock bounds loose enough for a slow CI runner; they catch a wait on the 5 s busy
    // timeout. The measured numbers are in
    // docs/benchmarks/2026-11-22-import-gui-latency.md; the checks that the worker never
    // writes and that saves interleave with an import are the exact ones.
    static constexpr qint64 kMaxStallMs = 1000;
    static constexpr qint64 kMaxSaveMs = 1000;
    int m_libraries = 0;
    int m_folders = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    SongLibrary *m_songs = nullptr;
    LiveState *m_live = nullptr;
    ServiceList *m_services = nullptr;
};

QTEST_MAIN(OperatorTest)
#include "tst_operator.moc"
