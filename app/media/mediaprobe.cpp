#include "media/mediaprobe.h"
#include "media/medialimits.h"

#include <QImageReader>

namespace visualis {

namespace {

// A four-character code as Core Media packs it, without multi-character literals (which GCC
// warns about).
constexpr quint32 fourCC(const char (&c)[5])
{
    return quint32(quint8(c[0])) << 24 | quint32(quint8(c[1])) << 16 | quint32(quint8(c[2])) << 8 | quint8(c[3]);
}

MediaProbe probeImage(const QString &path)
{
    MediaProbe out;
    QImageReader reader(path);
    reader.setAllocationLimit(medialimits::kImageAllocationMb);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (!size.isValid()) {
        out.error = reader.error() == QImageReader::UnknownError
                        ? QStringLiteral("not an image Visualis can read")
                        : QStringLiteral("not an image Visualis can read (%1)").arg(reader.errorString());
        return out;
    }
    // Width and height as shown: a photo taken sideways is stored rotated.
    if (reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();
    out.width = size.width();
    out.height = size.height();
    // Decoding the whole image (up to the limit) proves it isn't truncated or corrupt.
    const QImage full = reader.read();
    if (full.isNull()) {
        out.error = QStringLiteral("the image is damaged or unsupported (%1)").arg(reader.errorString());
        return out;
    }
    out.thumbnail = full.scaled(QSize(medialimits::kThumbnailPixels, medialimits::kThumbnailPixels),
                                Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}

} // namespace

MediaProbe probeMedia(const QString &path, MediaAsset::Kind kind)
{
    MediaProbe out = kind == MediaAsset::Kind::Video ? probeVideo(path) : probeImage(path);
    if (out.ok() && !out.thumbnail.isNull()
        && (out.thumbnail.width() > medialimits::kThumbnailPixels || out.thumbnail.height() > medialimits::kThumbnailPixels))
        out.thumbnail = out.thumbnail.scaled(QSize(medialimits::kThumbnailPixels, medialimits::kThumbnailPixels),
                                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}

QString codecName(quint32 code)
{
    switch (code) {
    case fourCC("avc1"): case fourCC("avc3"): return QStringLiteral("H.264");
    case fourCC("hvc1"): case fourCC("hev1"): return QStringLiteral("HEVC");
    case fourCC("apch"): return QStringLiteral("ProRes 422 HQ");
    case fourCC("apcn"): return QStringLiteral("ProRes 422");
    case fourCC("apcs"): return QStringLiteral("ProRes 422 LT");
    case fourCC("apco"): return QStringLiteral("ProRes 422 Proxy");
    case fourCC("ap4h"): return QStringLiteral("ProRes 4444");
    case fourCC("ap4x"): return QStringLiteral("ProRes 4444 XQ");
    case fourCC("mp4v"): return QStringLiteral("MPEG-4");
    case fourCC("jpeg"): return QStringLiteral("Motion JPEG");
    case fourCC("av01"): return QStringLiteral("AV1");
    case fourCC("vp09"): return QStringLiteral("VP9");
    default: break;
    }
    QString out;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const char c = char((code >> shift) & 0xff);
        out += (c >= 0x20 && c < 0x7f) ? QChar(c) : QChar('?');
    }
    return out;
}

} // namespace visualis
