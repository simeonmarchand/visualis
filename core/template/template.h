#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

// Stored templates (plan 5.4) and the per-item and per-slide overrides on top of them
// (plan 5.2). Everything is resolution-independent: positions and sizes are fractions of
// the output (sizes and offsets of its height). The styles are Q_GADGETs so the outputs
// can bind to them (#93; registered for QML in app/live/livestateqml.h).
namespace visualis {

enum class TemplateKind { Lyrics, Scripture, Title, LowerThird, Stage };
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };

struct Shadow {
    Q_GADGET
    Q_PROPERTY(bool enabled MEMBER enabled)
    Q_PROPERTY(QString color MEMBER color)
    Q_PROPERTY(double opacity MEMBER opacity)
    Q_PROPERTY(double blur MEMBER blur)
    Q_PROPERTY(double offset MEMBER offset)

public:
    bool enabled = false;
    QString color = QStringLiteral("#000000");
    double opacity = 1.0;
    double blur = 0.0;       // 0–1, MultiEffect's scale
    double offset = 0.0;     // of the output's height, down and right

    bool operator==(const Shadow &) const = default;
};

struct Outline {
    Q_GADGET
    Q_PROPERTY(QString color MEMBER color)
    Q_PROPERTY(double width MEMBER width)

public:
    QString color = QStringLiteral("#000000");
    double width = 0.0;      // of the output's height; 0 draws none

    bool operator==(const Outline &) const = default;
};

// How one text region looks. Colors are "#rrggbb" or "#aarrggbb".
struct RegionStyle {
    Q_GADGET
    Q_PROPERTY(QRectF frame MEMBER frame)
    Q_PROPERTY(QString fontFamily MEMBER fontFamily)
    Q_PROPERTY(double maxSize MEMBER maxSize)
    Q_PROPERTY(double minSize MEMBER minSize)
    Q_PROPERTY(int weight MEMBER weight)
    Q_PROPERTY(QString color MEMBER color)
    // As Qt::Alignment, which QML's Text takes.
    Q_PROPERTY(int horizontalAlignment READ horizontalAlignment)
    Q_PROPERTY(int verticalAlignment READ verticalAlignment)
    Q_PROPERTY(double lineHeight MEMBER lineHeight)
    Q_PROPERTY(visualis::Shadow shadow MEMBER shadow)
    Q_PROPERTY(visualis::Outline outline MEMBER outline)

public:
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

    int horizontalAlignment() const;
    int verticalAlignment() const;
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
    Q_GADGET
    Q_PROPERTY(QString kind READ kindName)   // "cut" or "fade"
    Q_PROPERTY(int ms MEMBER ms)

public:
    TransitionKind kind = TransitionKind::Cut;
    int ms = 300;            // unused by a cut, kept so switching to a fade remembers it

    bool operator==(const Transition &) const = default;

    QString kindName() const { return kind == TransitionKind::Fade ? QStringLiteral("fade") : QStringLiteral("cut"); }
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
    TextRegion *region(const QString &id);
};

QString templateKindName(TemplateKind kind);                       // "lower_third"
std::optional<TemplateKind> templateKindFromName(const QString &name);

// Built-in templates live in code, not in the library, so they're always there (even when
// the template table can't be read) and can't be edited or deleted, only duplicated (#94).
//
// The id of the built-in Lyrics template, the same in every library.
QUuid builtInLyricsTemplateId();
// The Lyrics template: one region, "main", filling the safe area, with the look Phase 1
// shipped (Helvetica Neue DemiBold, white, a soft black shadow, cut between slides).
Template builtInLyricsTemplate();
QList<Template> builtInTemplates();
std::optional<Template> builtInTemplate(const QUuid &id);
bool isBuiltInTemplate(const QUuid &id);

// The smallest a frame can be dragged to, as a fraction of the output's width or height.
constexpr double kMinFrameSize = 0.05;
// `start` after dragging `edges` by `delta` (fractions of the output) in the template editor:
// all four edges move it, fewer resize it. It stays on the output and no smaller than
// kMinFrameSize, so a drag past the edge stops there.
QRectF draggedFrame(const QRectF &start, Qt::Edges edges, QPointF delta);

// The templates an item can use, by id: a value snapshot, so rendering does no I/O (#93).
using TemplateSet = QHash<QUuid, Template>;
// `stored` and the built-ins; a stored template can't replace a built-in.
TemplateSet templateSet(const QList<Template> &stored);

// Overrides (ServiceItem::overrides, Group::slideOverrides): the format, and the only code
// that edits them, are in overrides.h.

// The template an override picks, or null.
QUuid overrideTemplateId(const QJsonObject &overrides);

// The region's style with the overrides applied: an overridden property sticks, everything
// else follows the template. A value that isn't valid for its property is ignored (the
// template's value shows) and reported in `warnings`. Rules between properties (minSize at
// most maxSize, the frame on the output) are checked once, after every layer. nullopt if the
// template has no such region.
std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QList<QJsonObject> &layers, QStringList *warnings = nullptr);
std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QJsonObject &overrides, QStringList *warnings = nullptr);

} // namespace visualis
