#include "library/medialibrary.h"
#include "library/songlibrary.h"
#include "library/library.h"
#include "media/mediafiles.h"
#include "media/mediaprobe.h"
#include "media/mediastore.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

const QString fixtures = QStringLiteral(FIXTURES_DIR "/media");

QString copyFixture(const QString &name, const QString &dir, const QString &as = {})
{
    const QString to = QDir(dir).filePath(as.isEmpty() ? name : as);
    return QFile::copy(fixtures + '/' + name, to) ? to : QString();
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() ? path : QString();
}

QStringList filesUnder(const QString &folder, const QString &sub)
{
    QStringList out;
    QDirIterator it(QDir(folder).filePath(sub), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next();
    return out;
}

} // namespace

class MediaImportTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<SongLibrary> m_songs;
    std::unique_ptr<MediaLibrary> m_media;
    QTemporaryDir m_lib;

    bool importAndWait(const QList<QUrl> &urls)
    {
        QSignalSpy finished(m_media.get(), &MediaLibrary::importFinished);
        if (!m_media->importFiles(urls))
            return false;
        return finished.wait(30'000);
    }
    QVariantMap asset(const QString &name) const
    {
        for (const QVariant &v : m_media->assets()) {
            if (v.toMap().value("name") == name)
                return v.toMap();
        }
        return {};
    }

private slots:
    void init()
    {
        QVERIFY(m_lib.isValid());
        m_songs = std::make_unique<SongLibrary>();
        QVERIFY(m_songs->openLibrary(m_lib.filePath(QString::number(QRandomGenerator::global()->generate()))));
        m_media = std::make_unique<MediaLibrary>(m_songs.get());
    }
    void cleanup()
    {
        m_media.reset();
        m_songs.reset();
    }

    void probeReadsAnH264Clip()
    {
        const MediaProbe p = probeMedia(fixtures + "/h264-320x180-30fps-2s.mp4", MediaAsset::Kind::Video);
#ifdef Q_OS_MACOS
        QVERIFY2(p.ok(), qPrintable(p.error));
        QCOMPARE(p.width, 320);
        QCOMPARE(p.height, 180);
        QCOMPARE(qRound(p.frameRate), 30);
        QVERIFY2(qAbs(p.durationMs - 2000) <= 50, qPrintable(QString::number(p.durationMs)));
        QCOMPARE(p.codec, QStringLiteral("H.264"));
        QVERIFY(!p.thumbnail.isNull());
        QCOMPARE(p.thumbnail.size(), QSize(320, 180));
#else
        QVERIFY(p.ok());
        QCOMPARE(p.warnings.size(), 1);
#endif
    }

    // Plan 5.6: a codec this Mac can't decode in hardware is imported with a warning. Only
    // MPEG-4 Part 2 is asserted: a CI virtual machine may decode nothing in hardware.
    void aCodecWithoutHardwareDecodeWarns()
    {
#ifndef Q_OS_MACOS
        QSKIP("video probing needs macOS");
#endif
        const MediaProbe p = probeMedia(fixtures + "/mpeg4-160x90-25fps-1s.mp4", MediaAsset::Kind::Video);
        QVERIFY2(p.ok(), qPrintable(p.error));
        QCOMPARE(p.codec, QStringLiteral("MPEG-4"));
        QCOMPARE(p.warnings.size(), 1);
        QVERIFY2(p.warnings[0].contains("hardware"), qPrintable(p.warnings[0]));
    }

    void probeReadsAnImageAndScalesItsThumbnail()
    {
        QTemporaryDir in;
        QImage image(1600, 900, QImage::Format_RGB32);
        image.fill(Qt::darkBlue);
        QVERIFY(image.save(in.filePath("wide.png")));
        const MediaProbe p = probeMedia(in.filePath("wide.png"), MediaAsset::Kind::Image);
        QVERIFY2(p.ok(), qPrintable(p.error));
        QCOMPARE(p.width, 1600);
        QCOMPARE(p.height, 900);
        QCOMPARE(p.thumbnail.size(), QSize(320, 180));
    }

    void importStoresProbesAndThumbnails()
    {
        QTemporaryDir in;
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(in.filePath("Cross.png")));
        QVERIFY(!copyFixture("h264-320x180-30fps-2s.mp4", in.path(), "Sunrise.mp4").isEmpty());

        QVERIFY(importAndWait({QUrl::fromLocalFile(in.path())}));
        QVERIFY2(m_media->report().startsWith("Imported 2 files."), qPrintable(m_media->report()));
        const QVariantMap cross = asset("Cross.png");
        QCOMPARE(cross.value("kind").toString(), QStringLiteral("image"));
        QCOMPARE(cross.value("details").toString(), QStringLiteral("64 × 48"));
        QVERIFY(QFile::exists(cross.value("thumbnail").toUrl().toLocalFile()));
#ifdef Q_OS_MACOS
        const QVariantMap sunrise = asset("Sunrise.mp4");
        QCOMPARE(sunrise.value("kind").toString(), QStringLiteral("video"));
        QCOMPARE(sunrise.value("details").toString(), QStringLiteral("320 × 180 · 0:02 · 30 fps · H.264"));
        const QImage thumb(sunrise.value("thumbnail").toUrl().toLocalFile());
        QCOMPARE(thumb.size(), QSize(320, 180));
#endif
    }

    // The same content under another name, or imported twice, is one asset.
    void importDeduplicatesByContent()
    {
        QTemporaryDir in;
        QVERIFY(!copyFixture("h264-320x180-30fps-2s.mp4", in.path(), "Sunrise.mp4").isEmpty());
        QVERIFY(!copyFixture("h264-320x180-30fps-2s.mp4", in.path(), "Sunrise copy.mov").isEmpty());
        QVERIFY(importAndWait({QUrl::fromLocalFile(in.path())}));
        QCOMPARE(m_media->assets().size(), 1);
        QVERIFY2(m_media->report().contains("already in the library as \"Sunrise copy.mov\"")
                     || m_media->report().contains("already in the library as \"Sunrise.mp4\""),
                 qPrintable(m_media->report()));

        // #97 review (M5): one file too, not one per extension.
        QStringList stored = filesUnder(m_songs->library()->mediaPath(), "");
        stored.removeIf([](const QString &f) { return f.contains("/thumbs/"); });
        QCOMPARE(stored.size(), 1);

        QVERIFY(importAndWait({QUrl::fromLocalFile(in.filePath("Sunrise.mp4"))}));
        QCOMPARE(m_media->assets().size(), 1);
        QVERIFY(m_media->report().startsWith("Imported 0 files."));
    }

    // #97 review (H1): while another program holds the library's write lock, the save waits
    // on a timer, not on the GUI thread; it lands once the lock is let go.
    void aLockedLibraryDelaysTheSaveWithoutBlocking()
    {
        QTemporaryDir in;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        QVERIFY(image.save(in.filePath("Cross.png")));
        m_media->setLockWaitLimit(60'000);   // longer than any wait below, however slow the runner
        QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "other-program");
        other.setDatabaseName(m_songs->library()->databasePath());
        QVERIFY(other.open());
        QVERIFY(QSqlQuery(other).exec("BEGIN IMMEDIATE"));

        QElapsedTimer sinceTick;
        qint64 longestGap = 0;
        int ticks = 0;
        QTimer tick;
        tick.setInterval(10);
        connect(&tick, &QTimer::timeout, this, [&] {
            longestGap = qMax(longestGap, sinceTick.restart());
            ++ticks;
        });
        QSignalSpy finished(m_media.get(), &MediaLibrary::importFinished);
        QVERIFY(m_media->importFiles({QUrl::fromLocalFile(in.filePath("Cross.png"))}));
        sinceTick.start();
        tick.start();
        QTRY_VERIFY_WITH_TIMEOUT(m_media->importProgress().contains("Waiting for the library"), 5'000);
        // The window keeps running while the save waits: the timer goes on ticking, and the
        // save still hasn't happened. No fixed tick count, so a slow CI runner can't fail it.
        const int ticksBefore = ticks;
        QTRY_VERIFY_WITH_TIMEOUT(ticks - ticksBefore >= 10, 10'000);
        QVERIFY(m_media->assets().isEmpty());
        QVERIFY(QSqlQuery(other).exec("ROLLBACK"));
        QVERIFY(finished.wait(5'000));
        tick.stop();
        other.close();
        other = {};
        QSqlDatabase::removeDatabase("other-program");

        QCOMPARE(m_media->assets().size(), 1);
        QVERIFY2(longestGap < Library::kBusyTimeoutMs / 2, qPrintable(QString::number(longestGap)));
    }

    // H1: a lock held past the deadline fails the file clearly, and leaves no file behind.
    void aLibraryLockedTooLongFailsTheFileAndLeavesNothing()
    {
        QTemporaryDir in;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(in.filePath("Cross.png")));
        m_media->setLockWaitLimit(300);
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "other-program");
            other.setDatabaseName(m_songs->library()->databasePath());
            QVERIFY(other.open());
            QVERIFY(QSqlQuery(other).exec("BEGIN IMMEDIATE"));
            QVERIFY(importAndWait({QUrl::fromLocalFile(in.filePath("Cross.png"))}));
            QVERIFY(QSqlQuery(other).exec("ROLLBACK"));
            other.close();
        }
        QSqlDatabase::removeDatabase("other-program");
        QVERIFY2(m_media->report().contains("another program kept the library busy"), qPrintable(m_media->report()));
        QVERIFY(m_media->assets().isEmpty());
        QVERIFY2(filesUnder(m_songs->library()->mediaPath(), "").isEmpty(),
                 qPrintable(filesUnder(m_songs->library()->mediaPath(), "").join('\n')));
    }

    // #97 review (M4): deleting an asset while an import of the same content is under way
    // leaves the import's row with its file.
    void deletingDuringAnImportKeepsTheImportedFile()
    {
        QTemporaryDir in;
        QVERIFY(!copyFixture("h264-320x180-30fps-2s.mp4", in.path(), "Sunrise.mp4").isEmpty());
        QVERIFY(importAndWait({QUrl::fromLocalFile(in.filePath("Sunrise.mp4"))}));
        QCOMPARE(m_media->assets().size(), 1);
        const QString id = m_media->assets()[0].toMap().value("id").toString();

        QFile big(in.filePath("A long loop.mov"));
        QVERIFY(big.open(QIODevice::WriteOnly) && big.resize(qint64(300) * 1024 * 1024));
        big.close();
        QVERIFY(!copyFixture("h264-320x180-30fps-2s.mp4", in.path(), "Sunrise again.mp4").isEmpty());
        QSignalSpy finished(m_media.get(), &MediaLibrary::importFinished);
        QVERIFY(m_media->importFiles({QUrl::fromLocalFile(big.fileName()), QUrl::fromLocalFile(in.filePath("Sunrise again.mp4"))}));
        QTRY_VERIFY_WITH_TIMEOUT(!filesUnder(m_songs->library()->mediaPath(), ".partial").isEmpty(), 10'000);
        QVERIFY(m_media->removeAsset(id));   // while the worker is on the first file
        QVERIFY(finished.wait(60'000));

        MediaStore store(*m_songs->library());
        bool found = false;
        for (const MediaAsset &a : store.list()) {
            if (a.originalName == "Sunrise again.mp4") {
                found = true;
                QVERIFY2(QFileInfo(store.filePath(a)).isFile(), qPrintable(a.file));
            }
        }
        QVERIFY2(found, qPrintable(m_media->report()));
    }

    // Fail soft: damaged and unknown files are skipped with a reason, and nothing of them stays
    // in media/.
    void corruptFilesAreSkippedWithAReason()
    {
        QTemporaryDir in;
        QVERIFY(!writeFile(in.filePath("broken.png"), QByteArray("\x89PNG\r\n\x1a\n not really", 19)).isEmpty());
        QVERIFY(!writeFile(in.filePath("broken.mp4"), QByteArray(4096, '\x5a')).isEmpty());
        QVERIFY(!writeFile(in.filePath("notes.txt"), "hello").isEmpty());
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(in.filePath("fine.png")));

        QVERIFY(importAndWait({QUrl::fromLocalFile(in.filePath("broken.png")), QUrl::fromLocalFile(in.filePath("broken.mp4")),
                               QUrl::fromLocalFile(in.filePath("notes.txt")), QUrl::fromLocalFile(in.filePath("fine.png"))}));
        const QString report = m_media->report();
        QVERIFY2(report.contains("broken.png"), qPrintable(report));
        QVERIFY2(report.contains("notes.txt") && report.contains("not an image or video"), qPrintable(report));
#ifdef Q_OS_MACOS
        // Only macOS probes videos, so elsewhere broken.mp4 is imported too.
        QVERIFY2(report.startsWith("Imported 1 files."), qPrintable(report));
        QVERIFY2(report.contains("broken.mp4"), qPrintable(report));
        QCOMPARE(filesUnder(m_songs->library()->mediaPath(), "").size(), 2);   // fine.png and its thumbnail
#endif
    }

    // Stopping mid-copy removes the partial copy, and the GUI keeps running throughout.
    void aStoppedImportLeavesNoPartialFile()
    {
        QTemporaryDir in;
        QFile big(in.filePath("Long service loop.mov"));
        QVERIFY(big.open(QIODevice::WriteOnly));
        QVERIFY(big.resize(qint64(2) * 1024 * 1024 * 1024));   // sparse: fast to make, slow to copy
        big.close();

        QElapsedTimer sinceTick;
        qint64 longestGap = 0;
        QTimer tick;
        tick.setInterval(10);
        connect(&tick, &QTimer::timeout, this, [&] {
            longestGap = qMax(longestGap, sinceTick.restart());
        });
        QSignalSpy finished(m_media.get(), &MediaLibrary::importFinished);
        QVERIFY(m_media->importFiles({QUrl::fromLocalFile(big.fileName())}));
        sinceTick.start();
        tick.start();
        QTRY_VERIFY_WITH_TIMEOUT(!filesUnder(m_songs->library()->mediaPath(), ".partial").isEmpty(), 10'000);
        m_media->stopImport();
        QVERIFY(finished.wait(10'000));
        tick.stop();

        QVERIFY2(filesUnder(m_songs->library()->mediaPath(), "").isEmpty(),
                 qPrintable(filesUnder(m_songs->library()->mediaPath(), "").join('\n')));
        QVERIFY(m_media->assets().isEmpty());
        QVERIFY2(m_media->report().contains("stopped"), qPrintable(m_media->report()));
        QVERIFY2(longestGap < 1000, qPrintable(QString::number(longestGap)));   // measured: see the PR
        qInfo() << "longest GUI gap while copying" << longestGap << "ms";
    }

    // #97 review, second pass (L6): a thumbnail that can't be published is reported, and the
    // asset is still imported.
    void aThumbnailThatCantBeSavedIsAWarning()
    {
        QTemporaryDir in, outside;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::magenta);
        QVERIFY(image.save(in.filePath("Cross.png")));
        const QString media = m_songs->library()->mediaPath();
        QVERIFY(QDir().mkpath(media));
        QVERIFY(QFile::link(outside.path(), QDir(media).filePath("thumbs")));   // not the library's

        QVERIFY(importAndWait({QUrl::fromLocalFile(in.filePath("Cross.png"))}));
        QVERIFY2(m_media->report().startsWith("Imported 1 files."), qPrintable(m_media->report()));
        QVERIFY2(m_media->report().contains("Cross.png: no thumbnail"), qPrintable(m_media->report()));
        QVERIFY(filesUnder(outside.path(), "").isEmpty());
        QVERIFY(asset("Cross.png").value("thumbnail").toUrl().isEmpty());
    }

    void searchAndDelete()
    {
        QTemporaryDir in;
        for (const char *name : {"Easter Sunrise.png", "Cross.png"}) {
            QImage image(8, 8, QImage::Format_RGB32);
            image.fill(QColor::fromRgb(QRandomGenerator::global()->generate()));
            QVERIFY(image.save(in.filePath(name)));
        }
        QVERIFY(importAndWait({QUrl::fromLocalFile(in.path())}));
        QCOMPARE(m_media->assets().size(), 2);
        m_media->setSearch("sunrise");
        QCOMPARE(m_media->assets().size(), 1);
        const QString id = m_media->assets()[0].toMap().value("id").toString();
        const QString thumb = m_media->assets()[0].toMap().value("thumbnail").toUrl().toLocalFile();
        QVERIFY(m_media->usedBy(id).isEmpty());
        QVERIFY(m_media->removeAsset(id));
        QVERIFY(m_media->assets().isEmpty());
        QVERIFY(!QFile::exists(thumb));
        m_media->setSearch("");
        QCOMPARE(m_media->assets().size(), 1);
    }

    // Opening another library mid-import drops what's left for the old one.
    void switchingLibrariesStopsTheImport()
    {
        QTemporaryDir in, other;
        QFile big(in.filePath("Big.mov"));
        QVERIFY(big.open(QIODevice::WriteOnly) && big.resize(qint64(2) * 1024 * 1024 * 1024));
        big.close();
        const QString oldMedia = m_songs->library()->mediaPath();
        QSignalSpy finished(m_media.get(), &MediaLibrary::importFinished);
        QVERIFY(m_media->importFiles({QUrl::fromLocalFile(big.fileName())}));
        QVERIFY(m_songs->openLibrary(other.path()));
        QVERIFY(finished.wait(10'000));
        QVERIFY(m_media->assets().isEmpty());
        QVERIFY(filesUnder(oldMedia, "").isEmpty());
    }
};

QTEST_MAIN(MediaImportTest)
#include "tst_mediaimport.moc"
