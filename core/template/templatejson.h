#pragma once

#include "template.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace visualis {

// Templates as JSON (plan 5.9): the template table's body, and a file format for sharing.
//
// {
//   "format": "visualis-template", "version": 1,
//   "id": "…", "name": "Lyrics", "kind": "lyrics",   // lyrics|scripture|title|lower_third|stage
//   "revision": 1,
//   "safeArea": {"x": 0.06, "y": 0.08},
//   "transitions": {"content": {"kind": "cut", "ms": 300}, "background": {"kind": "fade", "ms": 600}},
//   "regions": [{
//     "id": "main", "name": "Lyrics",
//     "frame": {"x": 0.06, "y": 0.08, "w": 0.88, "h": 0.84},
//     "fontFamily": "Helvetica Neue", "maxSize": 0.085, "minSize": 0.035, "weight": 600,
//     "color": "#ffffff", "align": "center", "verticalAlign": "center", "lineHeight": 1.1,
//     "shadow": {"enabled": true, "color": "#000000", "opacity": 0.85, "blur": 0.5, "offset": 0.003},
//     "outline": {"color": "#000000", "width": 0}
//   }],
//   "created": "…", "updated": "…"
// }
//
// Reading fails soft: keys it doesn't know are kept and written back, and a value that's
// missing or not valid for its property takes the default and is reported. Only input that
// isn't a template at all (not JSON, another format, a newer version, too large) fails.

namespace templatelimits {
constexpr int kBytes = 256 * 1024;      // a template is a few KB; this is the whole document
constexpr int kRegions = 32;
constexpr int kTextLength = 200;        // names, ids, font families
} // namespace templatelimits

QByteArray templateToJson(const Template &tmpl);

struct TemplateJsonResult {
    std::optional<Template> tmpl;
    QString error;           // set when it can't be read as a template at all
    QStringList warnings;    // values that fell back to defaults
};
TemplateJsonResult templateFromJson(const QByteArray &json);

// One region's style as JSON, the keys above.
QJsonObject regionStyleToJson(const RegionStyle &style);
// `base` with every valid property in `json` applied; invalid ones are reported in
// `warnings` (prefixed with `where`) and leave base's value. Checks no rule between
// properties, so several layers can be applied before enforceRegionConstraints runs once.
RegionStyle applyRegionProperties(const RegionStyle &base, const QJsonObject &json, const QString &where,
                                  QStringList *warnings);
// The rules between properties: the frame stays on the output (else `fallback`'s frame) and
// minSize is at most maxSize (else both are maxSize). Breaking one is reported.
RegionStyle enforceRegionConstraints(RegionStyle style, const RegionStyle &fallback, const QString &where,
                                     QStringList *warnings);
// Both, for one layer over `base`, as reading a template does.
RegionStyle applyRegionStyle(const RegionStyle &base, const QJsonObject &json, const QString &where,
                             QStringList *warnings);

} // namespace visualis
