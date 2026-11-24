#pragma once

#include "library/medialibrary.h"
#include "live/livestate.h"

#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

// Makes core's LiveState the `LiveState` singleton in QML: the one state the operator
// window changes and every output renders (plan 5.2). Backgrounds are checked against the
// media library (#98).
struct LiveStateForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::LiveState)
    QML_NAMED_ELEMENT(LiveState)
    QML_SINGLETON

public:
    static visualis::LiveState *create(QQmlEngine *qml, QJSEngine *)
    {
        auto *live = new visualis::LiveState;
        QPointer<MediaLibrary> media = qml->singletonInstance<MediaLibrary *>("Visualis.Ui", "MediaLibrary");
        live->setMediaCheck([media](const QUuid &id) {
            return visualis::checkMedia(media ? media->snapshot() : visualis::MediaSnapshot{}, id);
        });
        return live;
    }
};

// The content layer's value types (#93), so outputs and previews can bind to
// LiveState.content.regions.main.style.fontFamily and declare typed properties.
struct SlideContentForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::Content)
    QML_VALUE_TYPE(slideContent)
};

struct ContentRegionForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::ContentRegion)
    QML_VALUE_TYPE(contentRegion)
};

struct RegionStyleForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::RegionStyle)
    QML_VALUE_TYPE(regionStyle)
};

struct TextShadowForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::Shadow)
    QML_VALUE_TYPE(textShadow)
};

struct TextOutlineForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::Outline)
    QML_VALUE_TYPE(textOutline)
};

struct LayerTransitionForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::Transition)
    QML_VALUE_TYPE(layerTransition)
};
