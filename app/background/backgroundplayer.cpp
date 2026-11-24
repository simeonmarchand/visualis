#include "background/backgroundplayer.h"

#include "background/backgroundsource.h"

#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QSettings>

using namespace visualis;

struct BackgroundPlayer::Slot {
    std::unique_ptr<BackgroundSource> video;
    std::unique_ptr<BackgroundSource> still;
    QUuid asset;   // null when empty
    bool empty = true;
    MediaAsset::Kind kind = MediaAsset::Kind::Image;
    double opacity = 0;
    quint64 imageGeneration = 0;   // so each new still is a new URL, which QML loads afresh

    BackgroundSource *source() const
    {
        return empty ? nullptr : kind == MediaAsset::Kind::Video ? video.get() : still.get();
    }
};

namespace {

const QString kFillModeKey = QStringLiteral("background/fillMode");

// Serves each slot's still to the outputs' Image items: "image://background/<slot>/<n>". The
// image is decoded already (StillSource), so a request only hands it over.
class BackgroundImages : public QQuickImageProvider
{
public:
    explicit BackgroundImages(BackgroundPlayer *player)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_player(player)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const QImage image = m_player ? m_player->image(id.section('/', 0, 0).toInt()) : QImage();
        if (size)
            *size = image.size();
        return image;
    }

private:
    QPointer<BackgroundPlayer> m_player;
};

} // namespace

BackgroundPlayer::BackgroundPlayer(LiveState *live, const SourceFactory &makeSource, QObject *parent)
    : QObject(parent)
    , m_live(live)
    , m_fillMode(QStringLiteral("fill"))
{
    for (int n = 0; n < 2; ++n) {
        auto slot = std::make_unique<Slot>();
        slot->video = makeSource(MediaAsset::Kind::Video);
        slot->still = makeSource(MediaAsset::Kind::Image);
        for (BackgroundSource *source : {slot->video.get(), slot->still.get()}) {
            connect(source, &BackgroundSource::ready, this, [this, n, source] { sourceReady(n, source); });
            connect(source, &BackgroundSource::failed, this,
                    [this, n, source](const QString &why) { sourceFailed(n, source, why); });
            connect(source, &BackgroundSource::statusChanged, this, &BackgroundPlayer::statusChanged);
        }
        m_slots[n] = std::move(slot);
    }
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const double t = value.toDouble();
        Slot &in = *m_slots[m_front];
        in.opacity = t;
        if (in.empty)   // fading to nothing: the old one fades out over black instead
            back().opacity = 1 - t;
        emit slotsChanged();
    });
    connect(&m_fade, &QVariantAnimation::finished, this, &BackgroundPlayer::endFade);
    connect(&m_clearFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_opacity = value.toDouble();
        emit opacityChanged();
    });

    if (!m_live)
        return;
    connect(m_live, &LiveState::changed, this, &BackgroundPlayer::layerChanged);
    // Whatever is live already shows, without a fade in: as an output window that opens.
    m_cleared = m_live->background().cleared;
    m_opacity = m_cleared ? 0 : 1;
    layerChanged();
}

BackgroundPlayer::~BackgroundPlayer() = default;

BackgroundPlayer *BackgroundPlayer::create(QQmlEngine *qml, QJSEngine *)
{
    auto *live = qml->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
    auto *player = new BackgroundPlayer(live, [](MediaAsset::Kind kind) -> std::unique_ptr<BackgroundSource> {
        if (kind == MediaAsset::Kind::Video)
            return std::make_unique<VideoSource>();
        return std::make_unique<StillSource>();
    });
    player->setFillMode(QSettings().value(kFillModeKey, player->fillMode()).toString());
    connect(player, &BackgroundPlayer::fillModeChanged, player,
            [player] { QSettings().setValue(kFillModeKey, player->fillMode()); });
    qml->addImageProvider(QStringLiteral("background"), new BackgroundImages(player));
    return player;
}

void BackgroundPlayer::addOutput(QVideoSink *slotA, QVideoSink *slotB)
{
    m_slots[0]->video->addTarget(slotA);
    m_slots[1]->video->addTarget(slotB);
}

void BackgroundPlayer::setFillMode(const QString &mode)
{
    const QString valid = mode == QLatin1String("fit") ? mode : QStringLiteral("fill");
    if (valid == m_fillMode)
        return;
    m_fillMode = valid;
    emit fillModeChanged();
}

void BackgroundPlayer::layerChanged()
{
    const BackgroundLayer &layer = m_live->background();
    if (layer.cleared != m_cleared) {
        m_cleared = layer.cleared;
        const double target = m_cleared ? 0 : 1;
        m_clearFade.stop();
        if (layer.transition.kind == TransitionKind::Fade && layer.transition.ms > 0) {
            m_clearFade.setStartValue(m_opacity);
            m_clearFade.setEndValue(target);
            m_clearFade.setDuration(layer.transition.ms);
            m_clearFade.start();
        } else {
            m_opacity = target;
            emit opacityChanged();
        }
    }
    // Every slide change lands here; only a different asset does anything.
    if (layer.asset != m_wanted) {
        m_wanted = layer.asset;
        play(layer.asset, layer.transition);
    }
}

void BackgroundPlayer::play(const QUuid &asset, const Transition &transition)
{
    finishFade();
    cancelPending();
    setAlert({});
    if (asset == m_slots[m_front]->asset) {   // back to what's showing
        emit statusChanged();
        return;
    }
    Slot &next = back();
    if (asset.isNull()) {
        m_pending = Pending{asset, transition};
        swapIn();
        return;
    }
    const MediaCheck check = m_live->checkMedia(asset);
    if (!check.ok() || check.path.isEmpty()) {
        setAlert(check.message.isEmpty() ? QStringLiteral("The background's file can't be found.") : check.message);
        return;
    }
    next.asset = asset;
    next.kind = check.kind;
    next.empty = false;
    m_pending = Pending{asset, transition};
    next.source()->load(check.path);
    emit statusChanged();
}

void BackgroundPlayer::sourceReady(int n, BackgroundSource *source)
{
    if (m_pending && n != m_front && m_slots[n]->source() == source)
        swapIn();
}

void BackgroundPlayer::sourceFailed(int n, BackgroundSource *source, const QString &why)
{
    if (!m_pending || n == m_front || m_slots[n]->source() != source)
        return;
    cancelPending();
    setAlert(QStringLiteral("The background can't be shown: %1").arg(why));
}

void BackgroundPlayer::swapIn()
{
    const Transition transition = std::exchange(m_pending, std::nullopt)->transition;
    const int old = m_front;
    m_front = 1 - m_front;
    Slot &in = *m_slots[m_front];
    if (!in.empty && in.kind == MediaAsset::Kind::Image)
        ++in.imageGeneration;
    if (transition.kind == TransitionKind::Fade && transition.ms > 0) {
        in.opacity = 0;
        m_fade.setDuration(transition.ms);
        m_fade.start();
    } else {
        in.opacity = 1;
        retire(old);
    }
    emit slotsChanged();
    emit statusChanged();
}

void BackgroundPlayer::finishFade()
{
    if (!fading())
        return;
    m_fade.stop();
    endFade();
}

void BackgroundPlayer::endFade()
{
    m_slots[m_front]->opacity = 1;
    retire(1 - m_front);
    emit slotsChanged();
}

void BackgroundPlayer::cancelPending()
{
    if (!m_pending)
        return;
    m_pending.reset();
    retire(1 - m_front);
}

void BackgroundPlayer::retire(int n)
{
    Slot &s = *m_slots[n];
    s.video->stop();
    s.still->stop();
    s.asset = {};
    s.empty = true;
    s.opacity = 0;
}

void BackgroundPlayer::setAlert(const QString &alert)
{
    if (alert == m_alert)
        return;
    m_alert = alert;
    emit statusChanged();
}

QString BackgroundPlayer::status() const
{
    if (m_pending)
        return QStringLiteral("Loading");
    const Slot &front = *m_slots[m_front];
    if (front.empty)
        return QStringLiteral("No background");
    return front.kind == MediaAsset::Kind::Video ? front.video->status() : QStringLiteral("Showing a still");
}

QString BackgroundPlayer::alert() const
{
    if (!m_alert.isEmpty())
        return m_alert;
    for (const Slot *s : {m_slots[1 - m_front].get(), m_slots[m_front].get()}) {
        if (const BackgroundSource *source = s->source(); source && !source->alert().isEmpty())
            return source->alert();
    }
    return {};
}

QImage BackgroundPlayer::image(int n) const
{
    return n == 0 || n == 1 ? m_slots[n]->still->image() : QImage();
}

QUuid BackgroundPlayer::showing() const
{
    return m_slots[m_front]->asset;
}

BackgroundSlot BackgroundPlayer::slot(int n) const
{
    const Slot &s = *m_slots[n];
    BackgroundSlot out;
    out.opacity = s.opacity;
    out.front = n == m_front;
    if (s.empty)
        return out;
    out.kind = s.kind == MediaAsset::Kind::Video ? QStringLiteral("video") : QStringLiteral("image");
    const bool loading = m_pending && n != m_front;   // a still being decoded isn't drawn yet
    if (s.kind == MediaAsset::Kind::Image && !loading)
        out.image = QUrl(QStringLiteral("image://background/%1/%2").arg(n).arg(s.imageGeneration));
    return out;
}
