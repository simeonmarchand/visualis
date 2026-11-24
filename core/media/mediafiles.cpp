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

#include <cerrno>
#include <cstdio>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace visualis {

namespace {
constexpr qint64 kChunkBytes = 1024 * 1024;
const QString kPartialFolder = QStringLiteral(".partial");

// Renames within one filesystem, or fails. QFile::rename falls back to copying when the two
// paths are on different volumes, which for a video would be a multi-GB copy on the GUI
// thread (#97 review); a failed rename instead fails the import.
bool renameWithoutCopying(const QString &from, const QString &to, int *error)
{
#ifdef Q_OS_WIN
    const bool ok = _wrename(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(from).utf16()),
                             reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(to).utf16()))
                    == 0;
#else
    const bool ok = ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
    *error = ok ? 0 : errno;
    return ok;
}

// Waits until the file's data is on the disk, not just in the OS's cache. Its row is committed
// right after it's published, so without this a power loss can leave a row whose file came
// back empty (#113). On macOS only F_FULLFSYNC flushes the drive's own cache too.
bool syncToDisk(QFile &file)
{
#ifdef Q_OS_WIN
    return _commit(file.handle()) == 0;
#else
#ifdef F_FULLFSYNC
    if (::fcntl(file.handle(), F_FULLFSYNC) == 0)
        return true;   // otherwise the filesystem doesn't support it: fall back to fsync
#endif
    return ::fsync(file.handle()) == 0;
#endif
}

// Reads `in` to the end in chunks, hashing each and handing it to `each`, which returns an
// error to stop (empty to go on). One loop, so copying and rehashing can't disagree on a hash.
template <typename Each>
FileHash readChunks(QFile &in, const std::atomic_bool *stop, Each each)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk(kChunkBytes, Qt::Uninitialized);
    for (;;) {
        if (stop && stop->load())
            return {{}, true, {}};
        const qint64 n = in.read(chunk.data(), kChunkBytes);
        if (n < 0)
            return {{}, false, in.errorString()};
        if (n == 0)
            break;
        const QByteArrayView view(chunk.constData(), n);
        hash.addData(view);
        if (QString error = each(view); !error.isEmpty())
            return {{}, false, std::move(error)};
    }
    return {QString::fromLatin1(hash.result().toHex()), false, {}};
}

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

QString partialFolder(const QString &mediaFolder, bool create)
{
    if (create && (!QDir().mkpath(mediaFolder) || !QDir(mediaFolder).mkpath(kPartialFolder)))
        return {};
    const QString path = containedPath(mediaFolder, kPartialFolder);
    return !path.isEmpty() && QFileInfo(path).isDir() ? path : QString();
}

QString newPartialPath(const QString &mediaFolder, const QString &suffix)
{
    return QDir(mediaFolder).filePath(kPartialFolder + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
}

PartialCopy copyToPartial(const QString &source, const QString &mediaFolder, qint64 maxBytes, const std::atomic_bool *stop)
{
    PartialCopy out;
    // Refused before opening: opening a FIFO blocks until something writes to it, and reading
    // an iCloud file that isn't downloaded waits on the network. Neither can be stopped (#115).
    if (const QFileInfo info(source); info.exists() && !info.isFile()) {
        out.error = QStringLiteral("not a regular file");
        return out;
    }
#ifdef SF_DATALESS
    if (struct stat st; ::stat(QFile::encodeName(source).constData(), &st) == 0 && (st.st_flags & SF_DATALESS)) {
        out.error = QStringLiteral("it's in iCloud and not downloaded to this Mac; download it in Finder first");
        return out;
    }
#endif
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
    if (partialFolder(mediaFolder, true).isEmpty()) {
        out.error = QStringLiteral("the media folder's .partial folder is missing, or a link elsewhere");
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

    qint64 copied = 0;
    const FileHash read = readChunks(in, stop, [&](QByteArrayView chunk) {
        copied += chunk.size();
        if (copied > maxBytes)   // it grew since it was opened
            return QStringLiteral("too large (the limit is %1 MB)").arg(maxBytes / (1024 * 1024));
        if (partial.write(chunk.constData(), chunk.size()) != chunk.size())
            return partial.errorString();
        return QString();
    });
    if (read.cancelled)
        return abandon({}, true);
    if (!read.ok())
        return abandon(read.error);
    if (!partial.flush())
        return abandon(partial.errorString());
    if (!syncToDisk(partial))
        return abandon(QStringLiteral("couldn't write the copy to disk"));
    partial.close();
    out.hash = read.hash;
    return out;
}

FileHash hashFile(const QString &path, const std::atomic_bool *stop)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return {{}, false, in.errorString()};
    return readChunks(in, stop, [](QByteArrayView) { return QString(); });
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
        int error = 0;
        if (renameWithoutCopying(partialPath, target, &error))
            return out;
        // Another import may have published the same content first.
        if (!sameAlreadyThere()) {
            QFile::remove(partialPath);
            out.error = error == EXDEV
                            ? QStringLiteral("part of the media folder is on another disk, so the copy can't be moved in")
                            : QStringLiteral("couldn't move the copy into the media folder");
            return out;
        }
    }
    QFile::remove(partialPath);
    out.existed = true;
    return out;
}

void removeStalePartialCopies(const QString &mediaFolder, std::chrono::seconds olderThan)
{
    // Only the library's own folder: never one a link points at (#97 review).
    const QString folder = partialFolder(mediaFolder, false);
    if (folder.isEmpty())
        return;
    const QDir partial(folder);
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(olderThan.count() * -1);
    for (const QFileInfo &f : partial.entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
        if (!f.isSymLink() && f.lastModified(QTimeZone::UTC) < cutoff)
            QFile::remove(f.absoluteFilePath());
    }
}

void removeUnusedMediaFiles(const QString &mediaFolder, const QSet<QString> &inUse)
{
    const QString root = QFileInfo(mediaFolder).canonicalFilePath();
    if (root.isEmpty())
        return;
    // Only a plain file inside the media folder: containedPath refuses a link, or a file in a
    // linked folder.
    auto removeIfContained = [&](const QString &relative) {
        const QString path = containedPath(mediaFolder, relative);
        if (!path.isEmpty() && QFileInfo(path).isFile())
            QFile::remove(path);
    };
    static const QRegularExpression prefix(QStringLiteral("^[0-9a-f]{2}$"));
    const QDir media(root);
    for (const QFileInfo &sub : media.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (sub.isSymLink() || !prefix.match(sub.fileName()).hasMatch())
            continue;
        for (const QFileInfo &f : QDir(sub.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
            const QString hash = f.fileName().section('.', 0, 0);
            const QString relative = sub.fileName() + '/' + f.fileName();
            if (!f.isSymLink() && isValidMediaFile(relative, hash) && !inUse.contains(hash))
                removeIfContained(relative);
        }
    }
    const QFileInfo thumbs(media.filePath(QStringLiteral("thumbs")));
    if (!thumbs.isDir() || thumbs.isSymLink())
        return;
    for (const QFileInfo &f : QDir(thumbs.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
        const QString hash = f.fileName().section('.', 0, 0);
        if (!f.isSymLink() && isValidHash(hash) && f.fileName() == hash + QLatin1String(".jpg") && !inUse.contains(hash))
            removeIfContained(thumbnailFile(hash));
    }
}

} // namespace visualis
