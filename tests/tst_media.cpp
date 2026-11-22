#include "library/library.h"
#include "media/mediafiles.h"
#include "media/medialimits.h"
#include "media/mediastore.h"
#include "service/servicestore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        return {};
    return path;
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// Every file under the folder, relative to it.
QStringList filesUnder(const QString &folder)
{
    QStringList out;
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(folder).relativeFilePath(it.next());
    out.sort();
    return out;
}

} // namespace

class MediaTest : public QObject
{
    Q_OBJECT

private slots:
    void kindComesFromTheExtension()
    {
        QCOMPARE(mediaKindFor("Sunrise.MOV"), MediaAsset::Kind::Video);
        QCOMPARE(mediaKindFor("cross.jpeg"), MediaAsset::Kind::Image);
        QVERIFY(!mediaKindFor("notes.txt"));
        QVERIFY(!mediaKindFor("no extension"));
    }

    // The file goes under its SHA-256 and keeps its extension; the same bytes under another
    // name are the same file.
    void copyIsContentAddressedAndDeduplicated()
    {
        QTemporaryDir in, media;
        const QByteArray bytes = QByteArray("synthetic clip ").repeated(100'000);   // over one chunk
        const QString hash = sha256(bytes);

        const MediaCopy first = copyIntoMedia(writeFile(in.filePath("Sunrise.mp4"), bytes), media.path(),
                                              medialimits::kVideoBytes);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QCOMPARE(first.hash, hash);
        QCOMPARE(first.file, hash.left(2) + '/' + hash + ".mp4");
        QVERIFY(!first.existed);
        QFile stored(QDir(media.path()).filePath(first.file));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), bytes);

        const MediaCopy renamed = copyIntoMedia(writeFile(in.filePath("Sunrise (renamed).mp4"), bytes), media.path(),
                                                medialimits::kVideoBytes);
        QVERIFY(renamed.ok());
        QVERIFY(renamed.existed);
        QCOMPARE(renamed.file, first.file);
        QCOMPARE(filesUnder(media.path()), QStringList{first.file});   // no second copy, no partial
    }

    void aStoppedCopyLeavesNoPartialFile()
    {
        QTemporaryDir in, media;
        const QString source = writeFile(in.filePath("Long.mov"), QByteArray(5 * 1024 * 1024, 'x'));
        const std::atomic_bool stop{true};
        const MediaCopy r = copyIntoMedia(source, media.path(), medialimits::kVideoBytes, &stop);
        QVERIFY(r.cancelled);
        QVERIFY(!r.ok());
        QCOMPARE(filesUnder(media.path()), QStringList{});
    }

    void aFileOverTheCapIsRefusedBeforeCopying()
    {
        QTemporaryDir in, media;
        const QString source = writeFile(in.filePath("Big.png"), QByteArray(3 * 1024 * 1024, 'x'));
        const MediaCopy r = copyIntoMedia(source, media.path(), 2 * 1024 * 1024);
        QVERIFY(r.error.contains("too large"));
        QCOMPARE(filesUnder(media.path()), QStringList{});
    }

    void anUnreadableFileIsAnError()
    {
        QTemporaryDir media;
        const MediaCopy r = copyIntoMedia(media.filePath("missing.png"), media.path(), medialimits::kImageBytes);
        QVERIFY(!r.error.isEmpty());
    }

    // A crash mid-copy leaves a partial file; the next open of the library removes it.
    void leftoverPartialCopiesAreRemoved()
    {
        QTemporaryDir media;
        QVERIFY(QDir(media.path()).mkpath(".partial"));
        QVERIFY(!writeFile(media.filePath(".partial/abc"), "half").isEmpty());
        QVERIFY(!writeFile(media.filePath("keep.txt"), "kept").isEmpty());
        removePartialCopies(media.path());
        QCOMPARE(filesUnder(media.path()), QStringList{"keep.txt"});
    }

    void storeSavesLoadsAndRefusesADuplicateHash()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        MediaAsset a{{}, "aa11", "aa/aa11.mp4", "Sunrise.mp4", MediaAsset::Kind::Video, 1920, 1080, 30'000, 29.97,
                     "H.264", {}};
        QVERIFY(store.save(a).ok);
        QVERIFY(!a.id.isNull());
        QVERIFY(a.created.isValid());

        const std::optional<MediaAsset> loaded = store.load(a.id);
        QVERIFY(loaded);
        QCOMPARE(loaded->originalName, QStringLiteral("Sunrise.mp4"));
        QCOMPARE(loaded->kind, MediaAsset::Kind::Video);
        QCOMPARE(loaded->width, 1920);
        QCOMPARE(loaded->durationMs, 30'000);
        QCOMPARE(loaded->frameRate, 29.97);
        QCOMPARE(loaded->codec, QStringLiteral("H.264"));
        QCOMPARE(store.findByHash("aa11")->id, a.id);
        QVERIFY(!store.findByHash("bb22"));

        MediaAsset again{{}, "aa11", "aa/aa11.mov", "Copy.mov", MediaAsset::Kind::Video, 0, 0, 0, 0, {}, {}};
        const MediaStore::Result dup = store.save(again);
        QVERIFY(!dup.ok);
        QVERIFY2(dup.error.contains("already"), qPrintable(dup.error));
        QCOMPARE(store.list().size(), 1);

        a.originalName = "Sunrise loop.mp4";
        QVERIFY(store.save(a).ok);
        QCOMPARE(store.load(a.id)->originalName, a.originalName);
    }

    void listIsSortedByNameIgnoringCase()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        for (const char *name : {"waves.mp4", "Cross.png", "aurora.mov"}) {
            MediaAsset a{{}, name, QString(name), name, MediaAsset::Kind::Image, 0, 0, 0, 0, {}, {}};
            QVERIFY(store.save(a).ok);
        }
        QStringList names;
        for (const MediaAsset &a : store.list())
            names << a.originalName;
        QCOMPARE(names, (QStringList{"aurora.mov", "Cross.png", "waves.mp4"}));
    }

    // Deleting asks first when a service shows the asset.
    void usedByNamesTheServicesShowingIt()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore media(*library);
        MediaAsset used{{}, "aa11", "aa/aa11.mp4", "Sunrise.mp4", MediaAsset::Kind::Video, 0, 0, 0, 0, {}, {}};
        MediaAsset unused{{}, "bb22", "bb/bb22.png", "Cross.png", MediaAsset::Kind::Image, 0, 0, 0, 0, {}, {}};
        QVERIFY(media.save(used).ok && media.save(unused).ok);

        ServiceStore services(*library);
        Service s;
        s.title = "Sunday";
        s.date = QDate(2026, 12, 6);
        ServiceItem item;
        item.kind = ServiceItem::Kind::Media;
        item.title = "Sunrise";
        item.ref = used.id;
        s.items << item;
        QVERIFY(services.save(s).ok);

        QCOMPARE(media.usedBy(used.id), QStringList{"Sunday"});
        QVERIFY(media.usedBy(unused.id).isEmpty());
    }

    void removeDeletesTheRowFileAndThumbnail()
    {
        QTemporaryDir lib, in;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        const MediaCopy c = copyIntoMedia(writeFile(in.filePath("Cross.png"), "not really a png"), library->mediaPath(),
                                          medialimits::kImageBytes);
        QVERIFY(c.ok());
        MediaAsset a{{}, c.hash, c.file, "Cross.png", MediaAsset::Kind::Image, 0, 0, 0, 0, {}, {}};
        QVERIFY(store.save(a).ok);
        QVERIFY(QDir(library->mediaPath()).mkpath("thumbs"));
        QVERIFY(!writeFile(store.thumbnailPath(a), "thumb").isEmpty());

        QVERIFY(store.remove(a.id));
        QVERIFY(!store.load(a.id));
        QVERIFY(!QFile::exists(store.filePath(a)));
        QVERIFY(!QFile::exists(store.thumbnailPath(a)));
        QVERIFY(!store.remove(a.id));
    }

    // A library from before the media library gains an empty media_asset table and keeps its
    // songs and services.
    void anExistingLibraryGainsTheMediaTable()
    {
        QTemporaryDir dir;
        const std::vector<Migration> &all = Library::migrations();
        const auto before = std::find_if(all.begin(), all.end(), [](const Migration &m) {
            return m.statements.join(' ').contains("media_asset");
        });
        QVERIFY(before != all.end());
        QUuid serviceId;
        {
            auto old = Library::open(dir.path(), std::vector<Migration>(all.begin(), before));
            QVERIFY2(old.library, qPrintable(old.message));
            Service s;
            s.title = "Kept";
            QVERIFY(ServiceStore(*old.library).save(s).ok);
            serviceId = s.id;
        }
        auto now = Library::open(dir.path());
        QVERIFY2(now.library, qPrintable(now.message));
        QCOMPARE(now.library->schemaVersion(), Library::latestVersion());
        QCOMPARE(ServiceStore(*now.library).load(serviceId)->title, QStringLiteral("Kept"));
        MediaStore store(*now.library);
        QVERIFY(store.list().isEmpty());
        MediaAsset a{{}, "aa11", "aa/aa11.png", "Cross.png", MediaAsset::Kind::Image, 0, 0, 0, 0, {}, {}};
        QVERIFY(store.save(a).ok);
    }
};

QTEST_GUILESS_MAIN(MediaTest)
#include "tst_media.moc"
