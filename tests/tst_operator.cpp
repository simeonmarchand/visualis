#include "library/songlibrary.h"
#include "live/livestate.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

const char *kLyrics =
    "Verse 1\nAmazing grace how sweet the sound\nThat saved a wretch like me\n"
    "I once was lost but now am found\nWas blind but now I see\n\n"
    "Chorus\nMy chains are gone\nI've been set free\n";

QStringList titles(const QVariantList &songs)
{
    QStringList out;
    for (const QVariant &s : songs)
        out << s.toMap().value("title").toString();
    return out;
}

} // namespace

class OperatorTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_songs = m_engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
        QVERIFY(m_songs && m_live);
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
        m_songs->importSongs(QUrl::fromLocalFile(FIXTURES_DIR "/songs"));
        QCOMPARE(titles(m_songs->songs()), (QStringList{"Amazing Grace", "Holy, Holy, Holy"}));
        QVERIFY(m_songs->report().startsWith("Imported 2 songs"));
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

private:
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
    int m_libraries = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    SongLibrary *m_songs = nullptr;
    LiveState *m_live = nullptr;
};

QTEST_MAIN(OperatorTest)
#include "tst_operator.moc"
