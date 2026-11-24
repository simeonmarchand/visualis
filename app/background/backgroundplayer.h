#pragma once

#include "live/livestate.h"
#include "media/mediaasset.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>

#include <QVideoSink>

class BackgroundSource;
class QJSEngine;
class QQmlEngine;

// One of the player's two slots, as an output draws it.
struct BackgroundSlot {
    Q_GADGET
    QML_VALUE_TYPE(backgroundSlot)
    Q_PROPERTY(QString kind MEMBER kind)      // "none", "video", or "image"
    Q_PROPERTY(QUrl image MEMBER image)       // a still's picture, from the "background" image provider
    Q_PROPERTY(double opacity MEMBER opacity)
    Q_PROPERTY(bool front MEMBER front)       // drawn on top: the one showing, or fading in

public:
    QString kind = QStringLiteral("none");
    QUrl image;
    double opacity = 0;
    bool front = false;
};

// Plays the live background layer (GLOSSARY.md "Background player", #98). It watches
// LiveState, resolves the layer's asset, and plays it through a video or a still source in one
// of two slots. Outputs only draw the slots, so they stay views of the live state (plan 5.2).
//
// A new background loads in the slot that isn't showing while the old one stays up, so nothing
// goes black in between, and swaps in once it has a picture: at once for a cut, or fading in
// over the old one for a fade. Afterwards the old slot is stopped, so a still never keeps a
// video decoder running, and two decoders run only while a video-to-video fade overlaps them
// (ADR 0001). A background that can't be shown leaves the old one up and is handed back to
// LiveState::backgroundFailed(), so the layer says what's showing and why.
// Clearing fades the whole layer out (or cuts) with the layer's transition, and keeps it playing
// so showing it again is instant.
class BackgroundPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(BackgroundSlot slotA READ slotA NOTIFY slotsChanged)
    Q_PROPERTY(BackgroundSlot slotB READ slotB NOTIFY slotsChanged)
    // The layer's opacity: 0 while cleared.
    Q_PROPERTY(double opacity READ opacity NOTIFY opacityChanged)
    // How stills meet the output's shape: "fill" (cropped to cover it) or "fit" (whole, with
    // bars). Saved in the settings.
    Q_PROPERTY(QString fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    // For the operator: what's playing, and trouble playing it (a stalled video); empty when
    // fine. Why a background was refused is LiveState.backgroundAlert.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString alert READ alert NOTIFY statusChanged)

public:
    using SourceFactory = std::function<std::unique_ptr<BackgroundSource>(visualis::MediaAsset::Kind)>;

    // Resolves assets with live->checkMedia(). `makeSource` is called four times here: a video
    // and a still for each slot. The app's makes VideoSource and StillSource.
    BackgroundPlayer(visualis::LiveState *live, const SourceFactory &makeSource, QObject *parent = nullptr);
    ~BackgroundPlayer() override;
    // Also registers the "background" image provider the slots' images come from.
    static BackgroundPlayer *create(QQmlEngine *qml, QJSEngine *);

    // An output's two video sinks, one per slot. Sinks that are destroyed drop out on their own.
    Q_INVOKABLE void addOutput(QVideoSink *slotA, QVideoSink *slotB);

    BackgroundSlot slotA() const { return slot(0); }
    BackgroundSlot slotB() const { return slot(1); }
    double opacity() const { return m_opacity; }
    QString fillMode() const { return m_fillMode; }
    void setFillMode(const QString &mode);
    QString status() const;
    QString alert() const;

    // Whether the asset's file is showing or loading, so the library won't delete it.
    bool inUse(const QUuid &asset) const;
    // How long a background may take to get its first picture before it's given up. For tests.
    void setLoadTimeout(int ms);

    // For the image provider: slot n's still.
    QImage image(int n) const;
    // For tests.
    int frontSlot() const { return m_front; }
    QUuid showing() const;
    bool fading() const { return m_fade.state() == QAbstractAnimation::Running; }

signals:
    void slotsChanged();
    void opacityChanged();
    void fillModeChanged();
    void statusChanged();

private:
    struct Slot;
    struct Pending {
        QUuid asset;
        visualis::Transition transition;
    };

    void layerChanged();
    void play(const QUuid &asset, const visualis::Transition &transition);
    // Slot n's source became ready or failed; ignored unless it's the one loading.
    void sourceReady(int n, BackgroundSource *source);
    void sourceFailed(int n, BackgroundSource *source, const QString &why);
    // Hands `asset` back to LiveState as one that can't show.
    void refuse(const QUuid &asset, const QString &why);
    // The loading background failed: drop it and refuse it.
    void giveUp(const QString &why);
    void swapIn();
    // A fade in progress ends at once; endFade() is its natural end.
    void finishFade();
    void endFade();
    void cancelPending();
    // Stops slot n's sources and empties it.
    void retire(int n);
    BackgroundSlot slot(int n) const;
    Slot &back() { return *m_slots[1 - m_front]; }

    QPointer<visualis::LiveState> m_live;
    std::array<std::unique_ptr<Slot>, 2> m_slots;
    int m_front = 0;
    QUuid m_wanted;   // the layer's asset last seen
    bool m_cleared = false;
    std::optional<Pending> m_pending;   // loading in the back slot
    QTimer m_loadTimer;
    QVariantAnimation m_fade;           // the front slot fading in
    QVariantAnimation m_clearFade;      // the layer's opacity
    double m_opacity = 1;
    QString m_fillMode;
};
