#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

// Stored templates (plan 5.4) and the per-item and per-slide overrides on top of them
// (plan 5.2). Everything is resolution-independent: positions and sizes are fractions of
// the output (sizes and offsets of its height), as in app/qml/LyricsTemplate.qml.
namespace visualis {

enum class TemplateKind { Lyrics, Scripture, Title, LowerThird, Stage };
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };

struct Shadow {
    bool enabled = false;
    QString color = QStringLiteral("#000000");
    double opacity = 1.0;
    double blur = 0.0;       // 0–1, MultiEffect's scale
    double offset = 0.0;     // of the output's height, down and right

    bool operator==(const Shadow &) const = default;
};

struct Outline {
    QString color = QStringLiteral("#000000");
    double width = 0.0;      // of the output's height; 0 draws none

    bool operator==(const Outline &) const = default;
};

// How one text region looks. Colors are "#rrggbb" or "#aarrggbb".
struct RegionStyle {
    QRectF frame{0, 0, 1, 1};      // fractions of the output's width and height
    QString fontFamily = QStringLiteral("Helvetica Neue");
    double maxSize = 0.085;         // of the output's height: the size short text gets
    double minSize = 0.035;         // shrink-to-fit stops here, then the last line elides
    int weight = 400;               // 1–1000, as QFont::Weight (600 is DemiBold)
    QString color = QStringLiteral("#ffffff");
    HAlign hAlign = HAlign::Center;
    VAlign vAlign = VAlign::Center;
    double lineHeight = 1.0;        // a multiple of the font's line height
    Shadow shadow;
    Outline outline;

    bool operator==(const RegionStyle &) const = default;
};

struct TextRegion {
    QString id;                     // stable within the template; overrides name it ("main")
    QString name;                   // for the editor
    RegionStyle style;
    QJsonObject extra;              // the region as read, so unknown keys survive a save

    bool operator==(const TextRegion &) const = default;
};

// How one layer changes (#96). Kinds Visualis doesn't know read as Cut, with a warning, so
// later kinds (slide, dissolve) can be added without breaking older files.
enum class TransitionKind { Cut, Fade };

struct Transition {
    TransitionKind kind = TransitionKind::Cut;
    int ms = 300;            // unused by a cut, kept so switching to a fade remembers it

    bool operator==(const Transition &) const = default;
};

struct Transitions {
    Transition content;                                  // the text, slide to slide
    Transition background{TransitionKind::Fade, 600};    // the video or image behind it

    bool operator==(const Transitions &) const = default;
};

struct Template {
    QUuid id;                       // null until first saved
    QString name;
    TemplateKind kind = TemplateKind::Lyrics;
    QList<TextRegion> regions;
    double safeAreaX = 0.06;        // the share of each edge kept clear, for the editor's guide
    double safeAreaY = 0.08;
    Transitions transitions;
    int revision = 0;               // bumped by every save, so a renderer can tell it changed
    QDateTime created;              // set by TemplateStore, UTC
    QDateTime updated;
    QJsonObject extra;              // the template as read, so unknown keys survive a save

    bool operator==(const Template &) const = default;

    const TextRegion *region(const QString &id) const;
};

QString templateKindName(TemplateKind kind);                       // "lower_third"
std::optional<TemplateKind> templateKindFromName(const QString &name);

// Built-in templates live in code, not in the library, so they're always there (even when
// the template table can't be read) and can't be edited or deleted, only duplicated (#94).
//
// The id of the built-in Lyrics template, the same in every library.
QUuid builtInLyricsTemplateId();
// The Lyrics template with exactly the values of today's LyricsTemplate.qml: one region,
// "main", filling the safe area, so nothing changes on screen when templates arrive.
Template builtInLyricsTemplate();
QList<Template> builtInTemplates();
std::optional<Template> builtInTemplate(const QUuid &id);
bool isBuiltInTemplate(const QUuid &id);

// Overrides, as stored in ServiceItem::overrides and per slide (Group::slideOverrides):
//
//   {"template": "<template id>",
//    "regions": {"main": {"color": "#ffcc00", "maxSize": 0.07, "shadow": {"opacity": 0.5},
//                         "frame": {"y": 0.6}}}}
//
// Every key is optional. Region properties use the template JSON's names (templatejson.h),
// nested objects key by key, so overriding shadow.opacity leaves the shadow's color following
// the template. "template" picks another template for the item; on a slide it's ignored.
// Layers apply in order, later winning: the item's overrides, then the slide's.
//
// Per-slide overrides are an array parallel to the group's slides (Group::slideOverrides).
// When lyrics text is edited (#95), the editor must remap that array by matching unchanged
// slide text, so an override follows its slide rather than its index.

// The template an override picks, or null.
QUuid overrideTemplateId(const QJsonObject &overrides);

// The region's style with the overrides applied: an overridden property sticks, everything
// else follows the template. A value that isn't valid for its property is ignored (the
// template's value shows) and reported in `warnings`. nullopt if the template has no such
// region.
std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QList<QJsonObject> &layers, QStringList *warnings = nullptr);
std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QJsonObject &overrides, QStringList *warnings = nullptr);

} // namespace visualis
