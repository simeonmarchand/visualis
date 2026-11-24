#pragma once

#include "template.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUuid>

// Overrides (GLOSSARY.md "Override"; plan 5.2, 5.4): a stored partial set of template
// properties, on a service item (ServiceItem::overrides) or a slide (Group::slideOverrides).
// An overridden property sticks; everything else follows the template.
//
//   {"template": "<template id>",
//    "regions": {"main": {"color": "#ffcc00", "maxSize": 0.07, "shadow": {"opacity": 0.5},
//                         "frame": {"y": 0.6}}},
//    "transition": {"kind": "fade", "ms": 400}}
//
// Every key is optional, and keys Visualis doesn't know are kept through every edit.
//  - "regions": per region id, properties named as in the template JSON (templatejson.h),
//    nested objects key by key, so overriding shadow.opacity leaves the shadow's color
//    following the template. A property is named by its path: "color", "shadow.opacity",
//    "frame.y".
//  - "template": another template for a service item. On a slide it's ignored.
//  - "transition": the text's transition (#96), on a slide or an item, keys as in the
//    template's transitions.content. Either key alone applies; the editor sets both.
//  - "background" (#99) comes with that issue.
// Layers apply in order, later winning: the item's overrides, then the slide's. liveItem()
// (live/liveitem.h) is the only reader, and this is the only code that edits them.
//
// Each function edits one override. With several slides selected, the caller loops.
namespace visualis::overrides {

// Every region property's path, sorted: "align", "color", "frame.h", ... "weight".
const QStringList &regionProperties();
// The value at `path` in a region's JSON (an override's, or regionStyleToJson's), or undefined.
QJsonValue propertyValue(const QJsonObject &region, const QString &path);

// The region properties `overrides` sets for `regionId`, as sorted paths.
QStringList overriddenProperties(const QJsonObject &overrides, const QString &regionId);

// How many properties `overrides` sets: region properties in every region, and the transition.
int propertyCount(const QJsonObject &overrides);

// Sets one region property. Empty when set; otherwise why not, and `overrides` is unchanged:
// an unknown path, or a value that isn't valid for it (the rules templates are read with).
// Rules between properties (minSize at most maxSize, the frame on the output) depend on the
// template, so the caller checks them on the effective style.
QString setProperty(QJsonObject &overrides, const QString &regionId, const QString &path, const QJsonValue &value);
// Removes one region property, and any object it leaves empty, so the template's value shows.
void resetProperty(QJsonObject &overrides, const QString &regionId, const QString &path);
// Removes every region property, the transition, and the template choice. Keys outside "regions" that
// Visualis doesn't know stay.
void resetAll(QJsonObject &overrides);

// A service item's template; a null id removes the choice.
void setTemplate(QJsonObject &overrides, const QUuid &templateId);

// The text's transition, on a slide or an item. Empty when set; otherwise why not (a length
// out of range), and `overrides` is unchanged. Keys Visualis doesn't know inside it stay.
QString setTransition(QJsonObject &overrides, const Transition &transition);
void resetTransition(QJsonObject &overrides);
bool overridesTransition(const QJsonObject &overrides);

} // namespace visualis::overrides
