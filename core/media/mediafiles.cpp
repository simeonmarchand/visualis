#include "media/mediafiles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUuid>

namespace visualis {

namespace {
constexpr qint64 kChunkBytes = 1024 * 1024;
const QString kPartialFolder = QStringLiteral(".partial");

QString extensionOf(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    static const QRegularExpression safe(QStringLiteral("^[a-z0-9]{1,10}$"));
    return safe.match(suffix).hasMatch() ? suffix : QString();
}
} // namespace

QString kindName(MediaAsset::Kind kind)
{
    return kind == MediaAsset::Kind::Video ? QStringLiteral("video") : QStringLiteral("image");
}

std::optional<MediaAsset::Kind> mediaKindFor(const QString &fileName)
{
    static const QSet<QString> images{"jpg", "jpeg", "png", "heic", "heif", "tif", "tiff", "bmp", "gif", "webp"};
    static const QSet<QString> videos{"mp4", "mov", "m4v"};
    const QString ext = extensionOf(fileName);
    if (images.contains(ext))
        return MediaAsset::Kind::Image;
    if (videos.contains(ext))
        return MediaAsset::Kind::Video;
    return std::nullopt;
}

QString newPartialPath(const QString &mediaFolder, const QString &suffix)
{
    return QDir(mediaFolder).filePath(kPartialFolder + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
}

PartialCopy copyToPartial(const QString &source, const QString &mediaFolder, qint64 maxBytes, const std::atomic_bool *stop)
{
    PartialCopy out;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        out.error = in.errorString();
        return out;
    }
    if (in.size() > maxBytes) {
        out.error = QStringLiteral("too large (%1 MB; the limit is %2 MB)")
                        .arg(in.size() / (1024 * 1024)).arg(maxBytes / (1024 * 1024));
        return out;
    }
    if (!QDir(mediaFolder).mkpath(kPartialFolder)) {
        out.error = QStringLiteral("couldn't create the media folder");
        return out;
    }
    out.extension = extensionOf(source);
    out.path = newPartialPath(mediaFolder, out.extension.isEmpty() ? QString() : '.' + out.extension);
    QFile partial(out.path);
    if (!partial.open(QIODevice::NewOnly | QIODevice::WriteOnly)) {
        out.error = partial.errorString();
        return out;
    }
    auto abandon = [&](const QString &error, bool cancelled = false) {
        partial.close();
        QFile::remove(out.path);
        out.error = error;
        out.cancelled = cancelled;
        return out;
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk(kChunkBytes, Qt::Uninitialized);
    qint64 copied = 0;
    for (;;) {
        if (stop && stop->load())
            return abandon({}, true);
        const qint64 n = in.read(chunk.data(), kChunkBytes);
        if (n < 0)
            return abandon(in.errorString());
        if (n == 0)
            break;
        copied += n;
        if (copied > maxBytes)   // it grew since it was opened
            return abandon(QStringLiteral("too large (the limit is %1 MB)").arg(maxBytes / (1024 * 1024)));
        hash.addData(QByteArrayView(chunk.constData(), n));
        if (partial.write(chunk.constData(), n) != n)
            return abandon(partial.errorString());
    }
    if (!partial.flush())
        return abandon(partial.errorString());
    partial.close();
    out.hash = QString::fromLatin1(hash.result().toHex());
    return out;
}

QString mediaFileFor(const QString &hash, const QString &extension)
{
    return hash.left(2) + '/' + hash + (extension.isEmpty() ? QString() : '.' + extension);
}

QString thumbnailFile(const QString &hash)
{
    return QStringLiteral("thumbs/%1.jpg").arg(hash);
}

bool isValidHash(const QString &hash)
{
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    return sha256.match(hash).hasMatch();
}

bool isValidMediaFile(const QString &file, const QString &hash)
{
    if (!isValidHash(hash))
        return false;
    static const QRegularExpression ext(QStringLiteral("^(\\.[a-z0-9]{1,10})?$"));
    const QString stem = mediaFileFor(hash, {});
    return file.startsWith(stem) && ext.match(file.mid(stem.size())).hasMatch();
}

QString containedPath(const QString &mediaFolder, const QString &relative)
{
    const QString root = QFileInfo(mediaFolder).canonicalFilePath();
    if (root.isEmpty() || relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1String("..")))
        return {};
    const QString path = QDir(root).filePath(relative);
    const QFileInfo file(path);
    if (file.isSymLink())
        return {};
    // The folder it's in must be the one it names, not a link elsewhere. A folder that
    // doesn't exist yet can't be a link.
    const QFileInfo dir(file.absolutePath());
    if (dir.isSymLink() || (dir.exists() && dir.canonicalFilePath() != QDir::cleanPath(file.absolutePath())))
        return {};
    return path;
}

Publish publishPartial(const QString &partialPath, const QString &mediaFolder, const QString &relative)
{
    Publish out;
    const QString target = containedPath(mediaFolder, relative);
    if (target.isEmpty()) {
        QFile::remove(partialPath);
        out.error = QStringLiteral("the media folder holds a link where a file should be");
        return out;
    }
    const qint64 size = QFileInfo(partialPath).size();
    auto sameAlreadyThere = [&] {
        const QFileInfo there(target);
        return there.exists() && there.isFile() && !there.isSymLink() && there.size() == size;
    };
    if (!sameAlreadyThere()) {
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            QFile::remove(partialPath);
            out.error = QStringLiteral("couldn't create the media folder");
            return out;
        }
        if (QFileInfo::exists(target))   // a damaged leftover of the wrong size: replace it
            QFile::remove(target);
        if (QFile::rename(partialPath, target))
            return out;
        // Another import may have published the same content first.
        if (!sameAlreadyThere()) {
            QFile::remove(partialPath);
            out.error = QStringLiteral("couldn't move the copy into the media folder");
            return out;
        }
    }
    QFile::remove(partialPath);
    out.existed = true;
    return out;
}

void removeStalePartialCopies(const QString &mediaFolder, std::chrono::seconds olderThan)
{
    const QDir partial(QDir(mediaFolder).filePath(kPartialFolder));
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(olderThan.count() * -1);
    for (const QFileInfo &f : partial.entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
        if (f.lastModified(QTimeZone::UTC) < cutoff)
            QFile::remove(f.absoluteFilePath());
    }
}

} // namespace visualis
