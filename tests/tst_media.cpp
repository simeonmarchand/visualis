#include "library/library.h"
#include "media/mediafiles.h"
#include "media/medialimits.h"
#include "media/mediastore.h"
#include "service/servicestore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;
using namespace std::chrono_literals;

namespace {

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        return {};
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
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

MediaAsset asset(const QString &name, MediaAsset::Kind kind = MediaAsset::Kind::Image)
{
    const QString hash = sha256(name.toUtf8());
    return MediaAsset{{}, hash, mediaFileFor(hash, QFileInfo(name).suffix().toLower()), name, kind, 0, 0, 0, 0, {}, {}};
}

// Copies the bytes in and publishes them, as an import does; returns the stored asset.
MediaAsset storeFile(MediaStore &store, Library &library, const QString &dir, const QString &name, const QByteArray &bytes)
{
    const PartialCopy c = copyToPartial(writeFile(QDir(dir).filePath(name), bytes), library.mediaPath(),
                                        medialimits::kImageBytes);
    MediaAsset a{{}, c.hash, mediaFileFor(c.hash, c.extension), name, MediaAsset::Kind::Image, 0, 0, 0, 0, {}, {}};
    publishPartial(c.path, library.mediaPath(), a.file);
    store.save(a);
    return a;
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

    void copyHashesIntoAPartialFileThatKeepsItsExtension()
    {
        QTemporaryDir in, media;
        const QByteArray bytes = QByteArray("synthetic clip ").repeated(100'000);   // over one chunk
        const PartialCopy c = copyToPartial(writeFile(in.filePath("Sunrise.MP4"), bytes), media.path(),
                                            medialimits::kVideoBytes);
        QVERIFY2(c.ok(), qPrintable(c.error));
        QCOMPARE(c.hash, sha256(bytes));
        QCOMPARE(c.extension, QStringLiteral("mp4"));
        QVERIFY(c.path.startsWith(media.filePath(".partial/")));
        QVERIFY(c.path.endsWith(".mp4"));
        QCOMPARE(readAll(c.path), bytes);
    }

    void publishingMovesTheCopyUnderItsHash()
    {
        QTemporaryDir in, media;
        const QByteArray bytes("one image");
        const PartialCopy c = copyToPartial(writeFile(in.filePath("a.png"), bytes), media.path(), medialimits::kImageBytes);
        const QString file = mediaFileFor(c.hash, c.extension);
        QCOMPARE(file, c.hash.left(2) + '/' + c.hash + ".png");
        const Publish p = publishPartial(c.path, media.path(), file);
        QVERIFY2(p.ok(), qPrintable(p.error));
        QVERIFY(!p.existed);
        QCOMPARE(filesUnder(media.path()), QStringList{file});
        QCOMPARE(readAll(media.filePath(file)), bytes);
    }

    // #97 review (M7): two imports of the same content race to publish it; the second finds
    // the first's file, drops its own copy, and succeeds.
    void publishingContentAlreadyThereSucceedsAndDropsTheCopy()
    {
        QTemporaryDir in, media;
        const QByteArray bytes("the same clip");
        const QString source = writeFile(in.filePath("a.mp4"), bytes);
        const PartialCopy first = copyToPartial(source, media.path(), medialimits::kVideoBytes);
        const PartialCopy second = copyToPartial(source, media.path(), medialimits::kVideoBytes);
        const QString file = mediaFileFor(first.hash, first.extension);
        QVERIFY(publishPartial(first.path, media.path(), file).ok());
        const Publish p = publishPartial(second.path, media.path(), file);
        QVERIFY2(p.ok(), qPrintable(p.error));
        QVERIFY(p.existed);
        QCOMPARE(filesUnder(media.path()), QStringList{file});
    }

    void aDamagedFileUnderTheHashIsReplaced()
    {
        QTemporaryDir in, media;
        const QByteArray bytes("the real content");
        const PartialCopy c = copyToPartial(writeFile(in.filePath("a.png"), bytes), media.path(), medialimits::kImageBytes);
        const QString file = mediaFileFor(c.hash, c.extension);
        QVERIFY(QDir(media.path()).mkpath(c.hash.left(2)));
        QVERIFY(!writeFile(media.filePath(file), "trunc").isEmpty());   // a crash mid-write elsewhere
        const Publish p = publishPartial(c.path, media.path(), file);
        QVERIFY(p.ok() && !p.existed);
        QCOMPARE(readAll(media.filePath(file)), bytes);
    }

    void aStoppedCopyLeavesNoPartialFile()
    {
        QTemporaryDir in, media;
        const QString source = writeFile(in.filePath("Long.mov"), QByteArray(5 * 1024 * 1024, 'x'));
        const std::atomic_bool stop{true};
        const PartialCopy r = copyToPartial(source, media.path(), medialimits::kVideoBytes, &stop);
        QVERIFY(r.cancelled);
        QVERIFY(!r.ok());
        QCOMPARE(filesUnder(media.path()), QStringList{});
    }

    void aFileOverTheCapIsRefusedBeforeCopying()
    {
        QTemporaryDir in, media;
        const QString source = writeFile(in.filePath("Big.png"), QByteArray(3 * 1024 * 1024, 'x'));
        const PartialCopy r = copyToPartial(source, media.path(), 2 * 1024 * 1024);
        QVERIFY(r.error.contains("too large"));
        QCOMPARE(filesUnder(media.path()), QStringList{});
    }

    void anUnreadableFileIsAnError()
    {
        QTemporaryDir media;
        const PartialCopy r = copyToPartial(media.filePath("missing.png"), media.path(), medialimits::kImageBytes);
        QVERIFY(!r.error.isEmpty());
    }

    // #97 review (M6): a partial copy that's still being written may be another Visualis's;
    // only ones a crash left long ago are swept.
    void onlyStalePartialCopiesAreRemoved()
    {
        QTemporaryDir media;
        QVERIFY(QDir(media.path()).mkpath(".partial"));
        const QString fresh = writeFile(media.filePath(".partial/active.mov"), "being copied");
        const QString stale = writeFile(media.filePath(".partial/crashed.mov"), "left behind");
        QFile old(stale);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-2 * 3600), QFileDevice::FileModificationTime));
        old.close();
        removeStalePartialCopies(media.path(), 1h);
        QCOMPARE(filesUnder(media.path()), QStringList{".partial/active.mov"});
        QVERIFY(QFile::exists(fresh));
    }

    // #114: a crash between publishing a file and saving its row leaves a file no row uses.
    // Only those are swept: files and thumbnails named for a hash no row has. Never a link,
    // never what a link points at, and nothing else in the folder.
    void onlyMediaFilesNoRowUsesAreRemoved()
    {
        QTemporaryDir media, outside;
        const QString used = sha256("used"), unused = sha256("unused"), linked = sha256("linked"), far = sha256("far");
        const QDir dir(media.path());
        for (const QString &sub : {used.left(2), unused.left(2), linked.left(2), QString("thumbs"), QString(".partial")})
            QVERIFY(dir.mkpath(sub));
        QVERIFY(!writeFile(dir.filePath(mediaFileFor(used, "mp4")), "in use").isEmpty());
        QVERIFY(!writeFile(dir.filePath(thumbnailFile(used)), "its thumbnail").isEmpty());
        QVERIFY(!writeFile(dir.filePath(mediaFileFor(unused, "mov")), "orphan").isEmpty());
        QVERIFY(!writeFile(dir.filePath(mediaFileFor(unused, {})), "orphan without an extension").isEmpty());
        QVERIFY(!writeFile(dir.filePath(thumbnailFile(unused)), "orphan thumbnail").isEmpty());
        QVERIFY(!writeFile(dir.filePath(unused.left(2) + "/notes.txt"), "not Visualis's").isEmpty());
        QVERIFY(!writeFile(dir.filePath("README.txt"), "not Visualis's").isEmpty());
        QVERIFY(!writeFile(dir.filePath(".partial/copying.mov"), "in progress").isEmpty());
        // A hash-named link to a file outside, and a hash-named folder that's a link outside.
        const QString victim = writeFile(outside.filePath("victim.mp4"), "not the library's");
        QVERIFY(QFile::link(victim, dir.filePath(mediaFileFor(linked, "mp4"))));
        QVERIFY(QDir(outside.path()).mkpath("far"));
        const QString farVictim = writeFile(outside.filePath("far/" + far + ".mp4"), "not the library's");
        QVERIFY(QFile::link(outside.filePath("far"), dir.filePath(far.left(2))));   // the four prefixes differ

        removeUnusedMediaFiles(media.path(), {used});

        QVERIFY(QFile::exists(dir.filePath(mediaFileFor(used, "mp4"))));
        QVERIFY(QFile::exists(dir.filePath(thumbnailFile(used))));
        QVERIFY(!QFile::exists(dir.filePath(mediaFileFor(unused, "mov"))));
        QVERIFY(!QFile::exists(dir.filePath(mediaFileFor(unused, {}))));
        QVERIFY(!QFile::exists(dir.filePath(thumbnailFile(unused))));
        QVERIFY(QFile::exists(dir.filePath(unused.left(2) + "/notes.txt")));
        QVERIFY(QFile::exists(dir.filePath("README.txt")));
        QVERIFY(QFile::exists(dir.filePath(".partial/copying.mov")));
        QVERIFY(QFileInfo(dir.filePath(mediaFileFor(linked, "mp4"))).isSymLink());
        QVERIFY(QFile::exists(victim));
        QVERIFY(QFile::exists(farVictim));
    }

    // #97 review, second pass (H1): a .partial that's a link elsewhere is neither swept nor
    // copied into.
    void aSymlinkedPartialFolderIsNeverUsed()
    {
        QTemporaryDir in, media, outside;
        const QString victim = writeFile(outside.filePath("old.mov"), "not the library's");
        QFile old(victim);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-48 * 3600), QFileDevice::FileModificationTime));
        old.close();
        QVERIFY(QFile::link(outside.path(), media.filePath(".partial")));

        removeStalePartialCopies(media.path(), 1h);
        QVERIFY(QFile::exists(victim));
        QVERIFY(partialFolder(media.path(), true).isEmpty());
        const PartialCopy c = copyToPartial(writeFile(in.filePath("a.png"), "pixels"), media.path(), medialimits::kImageBytes);
        QVERIFY(!c.ok());
        QCOMPARE(filesUnder(outside.path()), QStringList{"old.mov"});
    }

    // #97 review (H2): only the exact path a hash gives is ever joined to the media folder.
    void onlyAHashsOwnPathIsAValidFile()
    {
        const QString hash = sha256("x");
        QVERIFY(isValidMediaFile(mediaFileFor(hash, "mp4"), hash));
        QVERIFY(isValidMediaFile(mediaFileFor(hash, {}), hash));
        QVERIFY(!isValidMediaFile("../../victim", hash));
        QVERIFY(!isValidMediaFile("/etc/hosts", hash));
        QVERIFY(!isValidMediaFile(hash.left(2) + "/../" + hash + ".mp4", hash));
        QVERIFY(!isValidMediaFile(mediaFileFor(hash, "mp4"), sha256("y")));   // another hash's
        QVERIFY(!isValidMediaFile(mediaFileFor(hash, "mp4") + "/x", hash));
        QVERIFY(!isValidMediaFile("aa/aa.mp4", "aa"));                          // not a SHA-256
    }

    // H2: a tampered row pointing outside media/ loads with no file, and deleting it deletes
    // nothing outside.
    void aRowPointingOutsideTheMediaFolderTouchesNothing()
    {
        QTemporaryDir lib, outside;
        auto library = Library::open(lib.path()).library;
        const QString victim = writeFile(outside.filePath("victim.txt"), "keep me");
        const QString hash = sha256("tampered");
        QSqlQuery q(library->database());
        q.prepare("INSERT INTO media_asset (id, content_hash, file, original_name, kind, created_at) "
                  "VALUES (?, ?, ?, 'x.png', 'image', '')");
        const QUuid id = QUuid::createUuid();
        q.addBindValue(id.toString(QUuid::WithoutBraces));
        q.addBindValue(hash);
        q.addBindValue(QDir(library->mediaPath()).relativeFilePath(victim));   // ../../…/victim.txt
        QVERIFY(q.exec());

        MediaStore store(*library);
        const std::optional<MediaAsset> a = store.load(id);
        QVERIFY(a);
        QVERIFY(a->file.isEmpty());
        QVERIFY(store.filePath(*a).isEmpty());
        QVERIFY(store.remove(id).ok);
        QVERIFY(QFile::exists(victim));
        QVERIFY(!store.load(id));
    }

    // H2: a link in place of a hash folder, pointing elsewhere, isn't followed.
    void aSymlinkedFolderInsideMediaIsNotFollowed()
    {
        QTemporaryDir lib, in, outside;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        const MediaAsset a = storeFile(store, *library, in.path(), "Cross.png", "cross pixels");
        QVERIFY(QFile::exists(store.filePath(a)));

        // Replace the asset's folder with a link to a folder holding a file of the same name.
        const QString folder = QDir(library->mediaPath()).filePath(a.contentHash.left(2));
        QVERIFY(QDir(folder).removeRecursively());
        const QString victim = writeFile(QDir(outside.path()).filePath(QFileInfo(a.file).fileName()), "keep me");
        QVERIFY(QFile::link(outside.path(), folder));

        QVERIFY(store.filePath(a).isEmpty());
        QVERIFY(store.remove(a.id).ok);
        QVERIFY(QFile::exists(victim));
    }

    // The media folder itself may be a link, e.g. to an external drive.
    void aSymlinkedMediaFolderIsFine()
    {
        QTemporaryDir lib, drive, in;
        QVERIFY(QFile::link(drive.path(), lib.filePath("media")));
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        const MediaAsset a = storeFile(store, *library, in.path(), "Cross.png", "cross pixels");
        QVERIFY(!store.filePath(a).isEmpty());
        QCOMPARE(readAll(store.filePath(a)), QByteArray("cross pixels"));
        QVERIFY(store.remove(a.id).ok);
        QCOMPARE(filesUnder(drive.path()), QStringList{});
    }

    void storeSavesLoadsAndRefusesADuplicateHash()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        MediaAsset a = asset("Sunrise.mp4", MediaAsset::Kind::Video);
        a.width = 1920;
        a.height = 1080;
        a.durationMs = 30'000;
        a.frameRate = 29.97;
        a.codec = "H.264";
        QVERIFY(store.save(a).ok);
        QVERIFY(!a.id.isNull());
        QVERIFY(a.created.isValid());

        const std::optional<MediaAsset> loaded = store.load(a.id);
        QVERIFY(loaded);
        QCOMPARE(loaded->file, a.file);
        QCOMPARE(loaded->originalName, QStringLiteral("Sunrise.mp4"));
        QCOMPARE(loaded->kind, MediaAsset::Kind::Video);
        QCOMPARE(loaded->width, 1920);
        QCOMPARE(loaded->durationMs, 30'000);
        QCOMPARE(loaded->frameRate, 29.97);
        QCOMPARE(loaded->codec, QStringLiteral("H.264"));
        QCOMPARE(store.findByHash(a.contentHash)->id, a.id);
        QVERIFY(!store.findByHash(sha256("other")));

        MediaAsset again = a;
        again.id = QUuid();
        again.file = mediaFileFor(a.contentHash, "mov");
        const MediaStore::Result dup = store.save(again);
        QVERIFY(!dup.ok);
        QVERIFY2(dup.error.contains("already"), qPrintable(dup.error));
        QCOMPARE(store.list().size(), 1);

        MediaAsset outside = asset("x.png");
        outside.file = "../../x.png";
        QVERIFY(!store.save(outside).ok);

        a.originalName = "Sunrise loop.mp4";
        QVERIFY(store.save(a).ok);
        QCOMPARE(store.load(a.id)->originalName, a.originalName);
    }

    // H1: another program holding the write lock makes a save fail at once with `busy`, so the
    // GUI thread retries later instead of waiting out the 5 s busy timeout.
    void aSaveWhileAnotherProgramWritesIsBusyAtOnce()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        MediaAsset kept = asset("Kept.png");
        QVERIFY(store.save(kept).ok);
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "other-program");
            other.setDatabaseName(library->databasePath());
            QVERIFY(other.open());
            QVERIFY(QSqlQuery(other).exec("BEGIN IMMEDIATE"));

            QElapsedTimer t;
            t.start();
            MediaAsset a = asset("Cross.png");
            const MediaStore::Result saved = store.save(a);
            const MediaStore::Result removed = store.remove(kept.id);
            QVERIFY(!saved.ok && saved.busy);
            QVERIFY(!removed.ok && removed.busy);
            QVERIFY2(t.elapsed() < Library::kBusyTimeoutMs / 2, qPrintable(QString::number(t.elapsed())));

            QVERIFY(QSqlQuery(other).exec("ROLLBACK"));
            other.close();
        }
        QSqlDatabase::removeDatabase("other-program");
        MediaAsset a = asset("Cross.png");
        QVERIFY(store.save(a).ok);   // the busy timeout is back for everything else
    }

    void listIsSortedByNameIgnoringCase()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        for (const char *name : {"waves.mp4", "Cross.png", "aurora.mov"}) {
            MediaAsset a = asset(name);
            QVERIFY(store.save(a).ok);
        }
        QStringList names;
        for (const MediaAsset &a : store.list())
            names << a.originalName;
        QCOMPARE(names, (QStringList{"aurora.mov", "Cross.png", "waves.mp4"}));
    }

    void contentHashesListsEveryRowsHash()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore store(*library);
        QCOMPARE(store.contentHashes(), std::optional(QSet<QString>{}));
        MediaAsset a = asset("waves.mp4"), b = asset("Cross.png");
        QVERIFY(store.save(a).ok && store.save(b).ok);
        QCOMPARE(store.contentHashes(), std::optional(QSet<QString>{a.contentHash, b.contentHash}));
    }

    // Deleting asks first when a service shows the asset.
    void usedByNamesTheServicesShowingIt()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        MediaStore media(*library);
        MediaAsset used = asset("Sunrise.mp4", MediaAsset::Kind::Video);
        MediaAsset unused = asset("Cross.png");
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
        const MediaAsset a = storeFile(store, *library, in.path(), "Cross.png", "not really a png");
        QVERIFY(QDir(library->mediaPath()).mkpath("thumbs"));
        QVERIFY(!writeFile(store.thumbnailPath(a), "thumb").isEmpty());

        QVERIFY(store.remove(a.id).ok);
        QVERIFY(!store.load(a.id));
        QCOMPARE(filesUnder(library->mediaPath()), QStringList{});
        QVERIFY(!store.remove(a.id).ok);
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
        MediaAsset a = asset("Cross.png");
        QVERIFY(store.save(a).ok);
    }
};

QTEST_GUILESS_MAIN(MediaTest)
#include "tst_media.moc"
