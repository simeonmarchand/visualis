#include "background/backgroundsource.h"

#include "media/medialimits.h"

#include <QFuture>
#include <QGuiApplication>
#include <QImageReader>
#include <QPromise>
#include <QScreen>
#include <QThreadPool>

#include <cmath>

#include <memory>

using namespace visualis;

VideoSource::VideoSource(QObject *parent)
    : BackgroundSource(parent)
{
    connect(&m_engine, &VideoEngine::playing, this, [this] {
        if (std::exchange(m_loading, false))
            emit ready();
    });
    connect(&m_engine, &VideoEngine::statusChanged, this, &BackgroundSource::statusChanged);
}

void VideoSource::load(const QString &path)
{
    const QUrl url = QUrl::fromLocalFile(path);
    if (url == m_engine.source())
        m_engine.setSource({});   // started again, so it reports its first frame again
    m_loading = true;
    m_engine.setSource(url);
}

void VideoSource::stop()
{
    m_loading = false;
    m_engine.setSource({});
}

namespace {
constexpr int kMaxSide = 8192;   // the largest texture every Metal GPU takes
}

StillImage decodeStill(const QString &path, const QSize &bound)
{
    QImageReader reader(path);
    // The import's limit, so anything that imported can show. A format that can't scale while
    // decoding (PNG, TIFF, HEIC) is read whole first; JPEGs decode scaled.
    reader.setAllocationLimit(medialimits::kImageAllocationMb);
    reader.setAutoTransform(true);
    const QSize stored = reader.size();
    if (!stored.isValid())
        return {{}, QStringLiteral("not an image Visualis can read (%1)").arg(reader.errorString())};
    // The reader scales before it turns the image, so the scaled size is in stored orientation.
    const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
    const QSize shown = turned ? stored.transposed() : stored;
    QSize target = shown.scaled(bound, Qt::KeepAspectRatioByExpanding);
    // A panorama covering the output can be wider than a GPU texture can be, and an odd shape
    // can need many times the output's pixels: either is scaled down to fit, and shows a
    // little softer, rather than failing.
    if (target.width() > kMaxSide || target.height() > kMaxSide)
        target = target.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio);
    if (const double budget = 4.0 * bound.width() * bound.height(), area = double(target.width()) * target.height();
        area > budget)
        target = (QSizeF(target) * std::sqrt(budget / area)).toSize();
    if (target.width() < shown.width())
        reader.setScaledSize(turned ? target.transposed() : target);
    QImage image = reader.read();
    if (image.isNull())
        return {{}, QStringLiteral("the image is damaged or unsupported (%1)").arg(reader.errorString())};
    return {std::move(image), {}};
}

StillSource::StillSource(QObject *parent)
    : BackgroundSource(parent)
{
}

QSize StillSource::outputSize()
{
    QSize largest;
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QSize pixels = screen->size() * screen->devicePixelRatio();
        if (pixels.width() * pixels.height() > largest.width() * largest.height())
            largest = pixels;
    }
    return largest.isValid() ? largest : QSize(1920, 1080);
}

void StillSource::load(const QString &path)
{
    const quint64 generation = ++*m_generation;
    auto promise = std::make_shared<QPromise<StillImage>>();
    QFuture<StillImage> decoded = promise->future();
    // Continued on this thread, and dropped if this source is gone first.
    decoded.then(this, [this, generation](const StillImage &still) {
        if (generation != *m_generation)
            return;
        if (still.image.isNull()) {
            emit failed(still.error);
            return;
        }
        m_image = still.image;
        emit ready();
    });
    QThreadPool::globalInstance()->start([promise, path, bound = outputSize(), generation, current = m_generation] {
        promise->start();
        // A pick overtaken while it waited for a thread isn't decoded at all, so quick picks
        // don't stack up full-size decodes.
        promise->addResult(generation == *current ? decodeStill(path, bound) : StillImage{});
        promise->finish();
    });
}

void StillSource::stop()
{
    ++*m_generation;
    m_image = QImage();
}
