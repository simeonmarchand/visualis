#include "media/mediafiles.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace visualis {

namespace {
constexpr qint64 kChunkBytes = 1024 * 1024;
const QString kPartialFolder = QStringLiteral(".partial");

// The stored file keeps the original's extension, because players pick a demuxer by it.
QString extension(const QString &fileName)
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
    const QString ext = extension(fileName);
    if (images.contains(ext))
        return MediaAsset::Kind::Image;
    if (videos.contains(ext))
        return MediaAsset::Kind::Video;
    return std::nullopt;
}

MediaCopy copyIntoMedia(const QString &source, const QString &mediaFolder, qint64 maxBytes, const std::atomic_bool *stop)
{
    MediaCopy out;
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
    const QDir media(mediaFolder);
    if (!media.mkpath(kPartialFolder)) {
        out.error = QStringLiteral("couldn't create the media folder");
        return out;
    }
    const QString partialPath = media.filePath(kPartialFolder + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QFile partial(partialPath);
    if (!partial.open(QIODevice::WriteOnly)) {
        out.error = partial.errorString();
        return out;
    }
    auto abandon = [&](const QString &error) {
        partial.close();
        QFile::remove(partialPath);
        out.error = error;
        return out;
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk(kChunkBytes, Qt::Uninitialized);
    qint64 copied = 0;
    for (;;) {
        if (stop && stop->load()) {
            partial.close();
            QFile::remove(partialPath);
            out.cancelled = true;
            return out;
        }
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
    const QString ext = extension(source);
    out.file = out.hash.left(2) + '/' + out.hash + (ext.isEmpty() ? QString() : '.' + ext);
    const QString target = media.filePath(out.file);
    if (QFile::exists(target)) {
        out.existed = true;
        QFile::remove(partialPath);
        return out;
    }
    if (!media.mkpath(out.hash.left(2)) || !QFile::rename(partialPath, target)) {
        QFile::remove(partialPath);
        out.error = QStringLiteral("couldn't move the copy into the media folder");
    }
    return out;
}

QString thumbnailFile(const QString &hash)
{
    return QStringLiteral("thumbs/%1.jpg").arg(hash);
}

void removePartialCopies(const QString &mediaFolder)
{
    QDir partial(QDir(mediaFolder).filePath(kPartialFolder));
    if (partial.exists())
        partial.removeRecursively();
}

} // namespace visualis
