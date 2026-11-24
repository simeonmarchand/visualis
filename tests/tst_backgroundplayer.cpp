#include "background/backgroundplayer.h"
#include "background/backgroundsource.h"
#include "live/livestate.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

// Stands in for a video or still adapter: records what the player asks of it, and becomes
// ready (or fails) only when the test says.
class FakeSource : public BackgroundSource
{
public:
    explicit FakeSource(MediaAsset::Kind kind) : kind(kind) {}

    void load(const QString &p) override
    {
        path = p;
        running = true;
        ++loads;
    }
    void stop() override
    {
        running = false;
        path.clear();
    }
    QImage image() const override { return running && kind == MediaAsset::Kind::Image ? QImage(4, 4, QImage::Format_RGB32) : QImage(); }
    void becomeReady() { emit ready(); }
    void fail(const QString &why) { emit failed(why); }

    const MediaAsset::Kind kind;
    QString path;
    bool running = false;
    int loads = 0;
};

const Transition kCut{TransitionKind::Cut, 600};
const Transition kFade{TransitionKind::Fade, 200};

QByteArray jpeg(const QImage &image)
{
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    image.save(&b, "JPG", 90);
    return out;
}

// A JPEG whose EXIF says to show it turned (6: rotate 90° clockwise), as a phone writes a
// photo taken sideways. Qt's writer turns the pixels instead, so the tag is added by hand.
QByteArray withOrientation(const QByteArray &jpg, quint16 orientation)
{
    QByteArray tiff("MM\0*\0\0\0\x08", 8);                   // big-endian, first IFD at 8
    tiff += QByteArray("\0\x01", 2);                         // one entry
    tiff += QByteArray("\x01\x12\0\x03\0\0\0\x01", 8);       // Orientation, SHORT, count 1
    tiff += char(orientation >> 8);
    tiff += char(orientation & 0xff);
    tiff += QByteArray(2, '\0');
    tiff += QByteArray(4, '\0');                             // no next IFD
    const QByteArray app1 = QByteArray("Exif\0\0", 6) + tiff;
    const int length = int(app1.size()) + 2;
    QByteArray segment("\xFF\xE1", 2);
    segment += char(length >> 8);
    segment += char(length & 0xff);
    return jpg.left(2) + segment + app1 + jpg.mid(2);
}

QString write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() ? path : QString();
}

} // namespace

// The background player (#98): plays the live background layer through two slots, each with a
// video and a still adapter. Fakes stand in for the adapters, so these run without a decoder.
class BackgroundPlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_sources.clear();
        m_live = std::make_unique<LiveState>();
        m_live->setMediaCheck([this](const QUuid &id) {
            MediaCheck c;
            if (!m_media.contains(id)) {
                c.status = MediaStatus::Missing;
                c.message = QStringLiteral("\"x\" is missing from the media folder.");
                return c;
            }
            c.status = MediaStatus::Ok;
            c.kind = m_media[id].first;
            c.path = m_media[id].second;
            return c;
        });
        m_player = std::make_unique<BackgroundPlayer>(m_live.get(), [this](MediaAsset::Kind kind) {
            auto source = std::make_unique<FakeSource>(kind);
            m_sources << source.get();
            return std::unique_ptr<BackgroundSource>(std::move(source));
        });
        QCOMPARE(m_sources.size(), 4);   // a video and a still for each slot
    }

    void cleanup()
    {
        m_player.reset();
        m_live.reset();
        m_media.clear();
    }

    void switchesEachPairOfKinds_data()
    {
        QTest::addColumn<bool>("fromVideo");
        QTest::addColumn<bool>("toVideo");
        QTest::addColumn<bool>("fade");
        for (bool fade : {false, true}) {
            const char *how = fade ? "fade" : "cut";
            QTest::addRow("still to still, %s", how) << false << false << fade;
            QTest::addRow("still to video, %s", how) << false << true << fade;
            QTest::addRow("video to still, %s", how) << true << false << fade;
            QTest::addRow("video to video, %s", how) << true << true << fade;
        }
    }
    void switchesEachPairOfKinds()
    {
        QFETCH(bool, fromVideo);
        QFETCH(bool, toVideo);
        QFETCH(bool, fade);
        const QUuid a = add(fromVideo, "/media/a"), b = add(toVideo, "/media/b");
        show(a, kCut);
        QCOMPARE(running(), QStringList{"/media/a"});

        QVERIFY(m_live->setBackground(b, fade ? kFade : kCut));
        // The old one stays up while the new one loads: never black in between.
        QCOMPARE(running(), (QStringList{"/media/a", "/media/b"}));
        QCOMPARE(m_player->showing(), a);
        sourceFor("/media/b")->becomeReady();
        QCOMPARE(m_player->showing(), b);
        if (fade) {
            QCOMPARE(running(), (QStringList{"/media/a", "/media/b"}));   // both, while they overlap
            QTRY_COMPARE(running(), QStringList{"/media/b"});
        } else {
            QCOMPARE(running(), QStringList{"/media/b"});   // a cut never overlaps
        }
        QCOMPARE(front().opacity, 1.0);
        QCOMPARE(back().opacity, 0.0);
        QCOMPARE(front().kind, toVideo ? QStringLiteral("video") : QStringLiteral("image"));
        QCOMPARE(back().kind, QStringLiteral("none"));
        QCOMPARE(runningVideos(), toVideo ? 1 : 0);   // a still stops the decoder
    }

    // ADR 0001, one decoder per background: two run only while a fade overlaps them.
    void aVideoToVideoFadeRunsTwoDecodersThenOne()
    {
        const QUuid a = add(true, "/media/a"), b = add(true, "/media/b");
        show(a, kCut);
        QVERIFY(m_live->setBackground(b, Transition{TransitionKind::Fade, 400}));
        sourceFor("/media/b")->becomeReady();
        QCOMPARE(runningVideos(), 2);
        QTest::qWait(100);
        QVERIFY2(front().opacity > 0 && front().opacity < 1, qPrintable(QString::number(front().opacity)));
        QCOMPARE(back().opacity, 1.0);   // the old one stays opaque beneath until the end
        QTRY_COMPARE(runningVideos(), 1);
        QCOMPARE(front().opacity, 1.0);
    }

    void aFadeInterruptedByTheNextBackgroundEndsOnTheNewest()
    {
        const QUuid a = add(false, "/media/a"), b = add(true, "/media/b"), c = add(false, "/media/c");
        show(a, kCut);
        QVERIFY(m_live->setBackground(b, Transition{TransitionKind::Fade, 2000}));
        sourceFor("/media/b")->becomeReady();
        QVERIFY(m_player->fading());

        QVERIFY(m_live->setBackground(c, kCut));
        // The fade went at once, so the one fading out is free for the newest.
        QVERIFY(!m_player->fading());
        QCOMPARE(m_player->showing(), b);
        QCOMPARE(running(), (QStringList{"/media/b", "/media/c"}));
        sourceFor("/media/c")->becomeReady();
        QCOMPARE(m_player->showing(), c);
        QCOMPARE(running(), QStringList{"/media/c"});
    }

    void aLoadOvertakenByTheNextBackgroundEndsOnTheNewest()
    {
        const QUuid a = add(false, "/media/a"), b = add(false, "/media/b"), c = add(true, "/media/c");
        show(a, kCut);
        QVERIFY(m_live->setBackground(b, kCut));
        FakeSource *slow = sourceFor("/media/b");   // a still decoding
        QVERIFY(m_live->setBackground(c, kCut));
        QCOMPARE(running(), (QStringList{"/media/a", "/media/c"}));
        slow->becomeReady();   // too late: it's been replaced
        QCOMPARE(m_player->showing(), a);
        sourceFor("/media/c")->becomeReady();
        QCOMPARE(m_player->showing(), c);
        QCOMPARE(running(), QStringList{"/media/c"});
    }

    void goingBackToWhatsShowingCancelsTheLoad()
    {
        const QUuid a = add(true, "/media/a"), b = add(true, "/media/b");
        show(a, kCut);
        QVERIFY(m_live->setBackground(b, kCut));
        QVERIFY(m_live->setBackground(a, kCut));
        QCOMPARE(running(), QStringList{"/media/a"});
        QCOMPARE(sourceFor("/media/a")->loads, 1);
    }

    // The same asset set again (as each slide of a song with that background will) changes
    // nothing: the video isn't restarted.
    void theSameBackgroundAgainDoesNotRestartIt()
    {
        const QUuid a = add(true, "/media/a");
        show(a, kCut);
        m_live->show(liveSong(song(), {}, templateSet({})), 0);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(m_live->setBackground(a, kFade));
            m_live->next();
        }
        QCOMPARE(sourceFor("/media/a")->loads, 1);
        QCOMPARE(running(), QStringList{"/media/a"});
    }

    void aBackgroundThatCantDecodeKeepsTheOldOneAndSaysWhy()
    {
        const QUuid a = add(false, "/media/a"), b = add(false, "/media/b");
        show(a, kCut);
        QVERIFY(m_live->setBackground(b, kCut));
        sourceFor("/media/b")->fail("the image is damaged");
        QCOMPARE(m_player->showing(), a);
        QCOMPARE(running(), QStringList{"/media/a"});
        QVERIFY2(m_player->alert().contains("the image is damaged"), qPrintable(m_player->alert()));
        QCOMPARE(front().opacity, 1.0);
    }

    // The layer checked it, but the file can still vanish before the player opens it.
    void aBackgroundWithNoFileKeepsTheOldOne()
    {
        const QUuid a = add(false, "/media/a"), b = QUuid::createUuid();
        show(a, kCut);
        m_media[b] = {MediaAsset::Kind::Image, QString()};
        QVERIFY(m_live->setBackground(b, kCut));
        QCOMPARE(m_player->showing(), a);
        QVERIFY(!m_player->alert().isEmpty());
    }

    void noBackgroundTakesTheOldOneAwayWithTheTransition()
    {
        const QUuid a = add(true, "/media/a");
        show(a, kCut);
        QVERIFY(m_live->setBackground({}, Transition{TransitionKind::Fade, 300}));
        QCOMPARE(m_player->showing(), QUuid());
        QTest::qWait(100);
        QVERIFY(back().opacity > 0 && back().opacity < 1);   // fading out over black
        QTRY_COMPARE(running(), QStringList{});
        QCOMPARE(front().kind, QStringLiteral("none"));
    }

    void clearingUsesTheLayersTransition()
    {
        const QUuid a = add(true, "/media/a");
        show(a, Transition{TransitionKind::Fade, 300});
        m_live->clearBackground();
        QTest::qWait(100);
        QVERIFY2(m_player->opacity() > 0 && m_player->opacity() < 1, qPrintable(QString::number(m_player->opacity())));
        QTRY_COMPARE(m_player->opacity(), 0.0);
        QCOMPARE(running(), QStringList{"/media/a"});   // cleared, not stopped: show is instant
        m_live->showBackground();
        QTRY_COMPARE(m_player->opacity(), 1.0);

        const QUuid b = add(true, "/media/b");
        show(b, kCut);
        m_live->clearBackground();
        QCOMPARE(m_player->opacity(), 0.0);   // a cut clears at once
    }

    void aPlayerMadeLaterShowsWhatsLive()
    {
        const QUuid a = add(false, "/media/a");
        m_live->setBackground(a, kCut);
        m_live->clearBackground();
        m_player.reset();
        m_sources.clear();
        m_player = std::make_unique<BackgroundPlayer>(m_live.get(), [this](MediaAsset::Kind kind) {
            auto source = std::make_unique<FakeSource>(kind);
            m_sources << source.get();
            return std::unique_ptr<BackgroundSource>(std::move(source));
        });
        QCOMPARE(m_player->opacity(), 0.0);
        sourceFor("/media/a")->becomeReady();
        QCOMPARE(m_player->showing(), a);
    }

    // The still adapter's decoding (#98): at output size, never full resolution, turned as
    // the photo's EXIF says.
    void aHugeImageDecodesAtOutputSize()
    {
        QTemporaryDir dir;
        QImage huge(6000, 4000, QImage::Format_Grayscale8);
        huge.fill(128);
        const QString path = write(dir.filePath("huge.jpg"), jpeg(huge));
        QVERIFY(!path.isEmpty());
        const StillImage still = decodeStill(path, QSize(1920, 1080));
        QVERIFY2(still.error.isEmpty(), qPrintable(still.error));
        QCOMPARE(still.image.size(), QSize(1920, 1280));   // covers 1920 x 1080 at 3:2
    }

    void aSmallImageIsNotEnlarged()
    {
        QTemporaryDir dir;
        QImage small(640, 480, QImage::Format_RGB32);
        small.fill(Qt::green);
        const QString path = write(dir.filePath("small.jpg"), jpeg(small));
        QCOMPARE(decodeStill(path, QSize(1920, 1080)).image.size(), QSize(640, 480));
    }

    void exifOrientationIsApplied()
    {
        QTemporaryDir dir;
        QImage stored(4000, 2000, QImage::Format_RGB32);   // left red, right blue
        stored.fill(Qt::red);
        QPainter(&stored).fillRect(2000, 0, 2000, 2000, Qt::blue);
        const QString path = write(dir.filePath("sideways.jpg"), withOrientation(jpeg(stored), 6));
        const StillImage still = decodeStill(path, QSize(1000, 1000));
        QVERIFY2(still.error.isEmpty(), qPrintable(still.error));
        QCOMPARE(still.image.size(), QSize(1000, 2000));   // shown upright: tall, then covering
        // Turned clockwise, the left (red) half is now on top.
        QVERIFY(QColor(still.image.pixel(500, 200)).red() > 200);
        QVERIFY(QColor(still.image.pixel(500, 1800)).blue() > 200);
    }

    void aDamagedImageSaysWhy()
    {
        QTemporaryDir dir;
        const QString path = write(dir.filePath("bad.jpg"), QByteArray("\xFF\xD8 not really a jpeg"));
        const StillImage still = decodeStill(path, QSize(1920, 1080));
        QVERIFY(still.image.isNull());
        QVERIFY(!still.error.isEmpty());
    }

private:
    QUuid add(bool video, const QString &path)
    {
        const QUuid id = QUuid::createUuid();
        m_media[id] = {video ? MediaAsset::Kind::Video : MediaAsset::Kind::Image, path};
        return id;
    }

    // Sets the background and lets it load.
    void show(const QUuid &id, const Transition &t)
    {
        QVERIFY(m_live->setBackground(id, t));
        if (FakeSource *s = sourceFor(m_media[id].second))
            s->becomeReady();
        QCOMPARE(m_player->showing(), id);
        QTRY_VERIFY(!m_player->fading());
    }

    FakeSource *sourceFor(const QString &path) const
    {
        for (FakeSource *s : m_sources) {
            if (s->running && s->path == path)
                return s;
        }
        return nullptr;
    }

    QStringList running() const
    {
        QStringList out;
        for (FakeSource *s : m_sources) {
            if (s->running)
                out << s->path;
        }
        out.sort();
        return out;
    }

    int runningVideos() const
    {
        return int(std::count_if(m_sources.begin(), m_sources.end(),
                                 [](FakeSource *s) { return s->running && s->kind == MediaAsset::Kind::Video; }));
    }

    BackgroundSlot front() const { return m_player->frontSlot() == 0 ? m_player->slotA() : m_player->slotB(); }
    BackgroundSlot back() const { return m_player->frontSlot() == 0 ? m_player->slotB() : m_player->slotA(); }

    static Song song()
    {
        Song s;
        s.title = "Joy to the World";   // public domain
        s.groups = {Group{QUuid::createUuid(), "Verse 1", "", {"Joy to the world", "Let earth receive her King",
                                                               "Let every heart", "Prepare Him room"}}};
        return s;
    }

    std::unique_ptr<LiveState> m_live;
    std::unique_ptr<BackgroundPlayer> m_player;
    QList<FakeSource *> m_sources;
    QHash<QUuid, std::pair<MediaAsset::Kind, QString>> m_media;
};

QTEST_MAIN(BackgroundPlayerTest)
#include "tst_backgroundplayer.moc"
