#include "media/mediafiles.h"
#include "media/mediaresolve.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// Flips one byte in place, keeping the size and the modification time, as silent disk
// corruption would.
bool flipAByte(const QString &path)
{
    const QDateTime modified = QFileInfo(path).lastModified(QTimeZone::UTC);
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    QByteArray bytes = f.readAll();
    bytes[bytes.size() / 2] = char(bytes[bytes.size() / 2] ^ 0x5a);
    f.seek(0);
    f.write(bytes);
    f.close();
    return f.open(QIODevice::ReadWrite) && f.setFileTime(modified, QFileDevice::FileModificationTime);
}

// A decoder that counts its calls and fails when told to.
struct FakeDecode {
    std::shared_ptr<int> calls = std::make_shared<int>(0);
    QString error;
    MediaDecode fn() const
    {
        return [calls = calls, error = error](const QString &, MediaAsset::Kind) {
            ++*calls;
            return error;
        };
    }
};

} // namespace

// The media resolver (#98): turns an asset id into a file to play and a status.
class MediaResolveTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_assets.clear();
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_media = m_dir->filePath("media");
        QVERIFY(QDir().mkpath(m_media));
    }

    void aStoredFileIsOk()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        const MediaCheck c = checkMedia(snapshot(), a.id);
        QCOMPARE(c.status, MediaStatus::Ok);
        QVERIFY(c.ok());
        QCOMPARE(c.path, QDir(QFileInfo(m_media).canonicalFilePath()).filePath(a.file));
        QCOMPARE(c.kind, MediaAsset::Kind::Image);
        QVERIFY(c.message.isEmpty());
    }

    void anUnknownIdSaysSo()
    {
        add("Sunrise.jpg", "sunrise pixels");
        const MediaCheck c = checkMedia(snapshot(), QUuid::createUuid());
        QCOMPARE(c.status, MediaStatus::UnknownId);
        QVERIFY(c.path.isEmpty());
        QVERIFY(!c.message.isEmpty());
    }

    void aDeletedFileIsMissing()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        QVERIFY(QFile::remove(QDir(m_media).filePath(a.file)));
        const MediaCheck c = checkMedia(snapshot(), a.id);
        QCOMPARE(c.status, MediaStatus::Missing);
        QVERIFY(c.path.isEmpty());
        QVERIFY2(c.message.contains("Sunrise.jpg"), qPrintable(c.message));
    }

    void anEmptyFileOrAFolderIsMissing()
    {
        const MediaAsset a = add("Empty.png", "x");
        QVERIFY(writeFile(QDir(m_media).filePath(a.file), {}));
        QCOMPARE(checkMedia(snapshot(), a.id).status, MediaStatus::Missing);

        const MediaAsset b = add("Folder.png", "y");
        const QString path = QDir(m_media).filePath(b.file);
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkpath(path));
        QCOMPARE(checkMedia(snapshot(), b.id).status, MediaStatus::Missing);
    }

    void aRowPointingOutsideTheFolderIsInvalid()
    {
        QVERIFY(writeFile(m_dir->filePath("x"), "outside"));
        MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        a.file = "../x";
        m_assets[0] = a;
        const MediaCheck c = checkMedia(snapshot(), a.id);
        QCOMPARE(c.status, MediaStatus::InvalidPath);
        QVERIFY(c.path.isEmpty());
    }

    void aSymlinkedHashFolderIsInvalid()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        const QString folder = QDir(m_media).filePath(a.contentHash.left(2));
        const QString elsewhere = m_dir->filePath("elsewhere");
        QVERIFY(QDir().rename(folder, elsewhere));
        QVERIFY(QFile::link(elsewhere, folder));
        QCOMPARE(checkMedia(snapshot(), a.id).status, MediaStatus::InvalidPath);
    }

    // The quick check is for show time: one stat, never a hash or a decode.
    void theQuickCheckPassesACorruptFile()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        QVERIFY(flipAByte(QDir(m_media).filePath(a.file)));
        QCOMPARE(checkMedia(snapshot(), a.id).status, MediaStatus::Ok);
    }

    void verificationFindsAFlippedByte()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        QVERIFY(flipAByte(QDir(m_media).filePath(a.file)));
        FakeDecode decode;
        const MediaCheck c = verifyMedia(snapshot(), a.id, decode.fn());
        QCOMPARE(c.status, MediaStatus::HashMismatch);
        QVERIFY(!c.path.isEmpty());   // it's there, just not what was imported
        QVERIFY(!c.verified);
    }

    void verificationDecodes()
    {
        const MediaAsset a = add("Motion.mp4", "motion frames", MediaAsset::Kind::Video);
        FakeDecode decode;
        const MediaCheck good = verifyMedia(snapshot(), a.id, decode.fn());
        QCOMPARE(good.status, MediaStatus::Ok);
        QCOMPARE(*decode.calls, 1);
        QVERIFY(good.verified);
        QCOMPARE(good.verified->size, qint64(QByteArray("motion frames").size()));

        decode.error = "not a video Visualis can play";
        const MediaCheck bad = verifyMedia(snapshot(), a.id, decode.fn());
        QCOMPARE(bad.status, MediaStatus::Undecodable);
        QVERIFY2(bad.message.contains("not a video Visualis can play"), qPrintable(bad.message));
    }

    void verificationStopsAtTheQuickCheckWithoutDecoding()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        QVERIFY(QFile::remove(QDir(m_media).filePath(a.file)));
        FakeDecode decode;
        QCOMPARE(verifyMedia(snapshot(), a.id, decode.fn()).status, MediaStatus::Missing);
        QCOMPARE(*decode.calls, 0);
    }

    // The session cache: a file unchanged since it was verified isn't hashed again, but is
    // still checked for and decoded.
    void aKnownGoodFileSkipsTheRehash()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        FakeDecode decode;
        const MediaCheck first = verifyMedia(snapshot(), a.id, decode.fn());
        QVERIFY(first.verified);
        QVERIFY(flipAByte(QDir(m_media).filePath(a.file)));   // same size, same time
        const MediaCheck again = verifyMedia(snapshot(), a.id, decode.fn(), nullptr, first.verified);
        QCOMPARE(again.status, MediaStatus::Ok);
        QCOMPARE(*decode.calls, 2);
    }

    void aChangedModificationTimeRehashes()
    {
        const MediaAsset a = add("Sunrise.jpg", "sunrise pixels");
        FakeDecode decode;
        const MediaCheck first = verifyMedia(snapshot(), a.id, decode.fn());
        const QString path = QDir(m_media).filePath(a.file);
        QVERIFY(flipAByte(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(first.verified->modified.addSecs(-60), QFileDevice::FileModificationTime));
        f.close();
        QCOMPARE(verifyMedia(snapshot(), a.id, decode.fn(), nullptr, first.verified).status, MediaStatus::HashMismatch);
    }

    void aStoppedVerificationIsNotAMismatch()
    {
        const MediaAsset a = add("Long.mov", QByteArray(8 * 1024 * 1024, 'x'), MediaAsset::Kind::Video);
        const std::atomic_bool stop{true};
        FakeDecode decode;
        QElapsedTimer t;
        t.start();
        const MediaCheck c = verifyMedia(snapshot(), a.id, decode.fn(), &stop);
        QVERIFY(t.elapsed() < 1000);
        QCOMPARE(c.status, MediaStatus::Stopped);
        QVERIFY(!c.verified);
        QCOMPARE(*decode.calls, 0);
    }

    void aSnapshotIsKeyedById()
    {
        const MediaAsset a = add("A.jpg", "a");
        const MediaAsset b = add("B.jpg", "b");
        const MediaSnapshot s = snapshot();
        QCOMPARE(s.folder, m_media);
        QCOMPARE(s.assets.size(), 2);
        QCOMPARE(s.assets.value(b.id).originalName, QStringLiteral("B.jpg"));
        QCOMPARE(s.assets.value(a.id).file, a.file);
    }

private:
    // Stores the bytes where an import would, and returns the asset's row.
    MediaAsset add(const QString &name, const QByteArray &bytes, MediaAsset::Kind kind = MediaAsset::Kind::Image)
    {
        const QString hash = sha256(bytes);
        MediaAsset a{QUuid::createUuid(), hash, mediaFileFor(hash, QFileInfo(name).suffix().toLower()),
                     name, kind, 0, 0, 0, 0, {}, {}};
        writeFile(QDir(m_media).filePath(a.file), bytes);
        m_assets << a;
        return a;
    }

    MediaSnapshot snapshot() const { return snapshotOf(m_media, m_assets); }

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_media;
    QList<MediaAsset> m_assets;
};

QTEST_GUILESS_MAIN(MediaResolveTest)
#include "tst_mediaresolve.moc"
