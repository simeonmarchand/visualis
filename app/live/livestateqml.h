#pragma once

#include "live/livestate.h"

#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// Makes core's LiveState the `LiveState` singleton in QML: the one state the operator
// window changes and every output renders (plan 5.2).
struct LiveStateForeign
{
    Q_GADGET
    QML_FOREIGN(visualis::LiveState)
    QML_NAMED_ELEMENT(LiveState)
    QML_SINGLETON

public:
    static visualis::LiveState *create(QQmlEngine *, QJSEngine *) { return new visualis::LiveState; }
};
