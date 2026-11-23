#include "template.h"
#include "templatejson.h"

namespace visualis {

namespace {

const QStringList kKindNames = {"lyrics", "scripture", "title", "lower_third", "stage"};

} // namespace

const TextRegion *Template::region(const QString &id) const
{
    for (const TextRegion &r : regions) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

QString templateKindName(TemplateKind kind)
{
    return kKindNames.value(int(kind));
}

std::optional<TemplateKind> templateKindFromName(const QString &name)
{
    const qsizetype i = kKindNames.indexOf(name);
    if (i < 0)
        return std::nullopt;
    return TemplateKind(i);
}

QUuid builtInLyricsTemplateId()
{
    return QUuid::fromString(QStringLiteral("749e037f-5f4b-40ca-ab87-6a3c56c84167"));
}

Template builtInLyricsTemplate()
{
    // Keep in step with app/qml/LyricsTemplate.qml until the outputs read templates (#93);
    // tst_template compares the two.
    Template t;
    t.id = builtInLyricsTemplateId();
    t.name = QStringLiteral("Lyrics");
    t.kind = TemplateKind::Lyrics;
    t.safeAreaX = 0.06;
    t.safeAreaY = 0.08;
    t.transitions.content = {TransitionKind::Cut, 300};
    t.transitions.background = {TransitionKind::Fade, 600};
    t.revision = 1;

    RegionStyle s;
    s.frame = QRectF(t.safeAreaX, t.safeAreaY, 1 - 2 * t.safeAreaX, 1 - 2 * t.safeAreaY);
    s.fontFamily = QStringLiteral("Helvetica Neue");
    s.maxSize = 0.085;
    s.minSize = 0.035;
    s.weight = 600;   // Font.DemiBold
    s.color = QStringLiteral("#ffffff");
    s.hAlign = HAlign::Center;
    s.vAlign = VAlign::Center;
    s.lineHeight = 1.1;
    s.shadow = {true, QStringLiteral("#000000"), 0.85, 0.5, 0.003};
    s.outline = {QStringLiteral("#000000"), 0};
    t.regions << TextRegion{QStringLiteral("main"), QStringLiteral("Lyrics"), s, {}};
    return t;
}

QList<Template> builtInTemplates()
{
    return {builtInLyricsTemplate()};
}

std::optional<Template> builtInTemplate(const QUuid &id)
{
    for (const Template &t : builtInTemplates()) {
        if (t.id == id)
            return t;
    }
    return std::nullopt;
}

bool isBuiltInTemplate(const QUuid &id)
{
    return builtInTemplate(id).has_value();
}

QUuid overrideTemplateId(const QJsonObject &overrides)
{
    return QUuid::fromString(overrides.value("template").toString());
}

std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QList<QJsonObject> &layers, QStringList *warnings)
{
    const TextRegion *region = tmpl.region(regionId);
    if (!region)
        return std::nullopt;
    RegionStyle style = region->style;
    for (const QJsonObject &layer : layers) {
        const QJsonValue regions = layer.value("regions");
        if (regions.isUndefined())
            continue;
        if (!regions.isObject()) {
            if (warnings)
                *warnings << QStringLiteral("an override's \"regions\" isn't an object; ignored");
            continue;
        }
        const QJsonValue mine = regions.toObject().value(regionId);
        if (mine.isUndefined())
            continue;
        if (!mine.isObject()) {
            if (warnings)
                *warnings << QStringLiteral("the override of region \"%1\" isn't an object; ignored").arg(regionId);
            continue;
        }
        style = applyRegionProperties(style, mine.toObject(), QStringLiteral("override of \"%1\"").arg(regionId), warnings);
    }
    // Once, on the final style: an item's small maxSize followed by a slide's large one must not
    // leave minSize clamped by the item's.
    return enforceRegionConstraints(style, region->style, QStringLiteral("override of \"%1\"").arg(regionId), warnings);
}

std::optional<RegionStyle> effectiveStyle(const Template &tmpl, const QString &regionId,
                                          const QJsonObject &overrides, QStringList *warnings)
{
    return effectiveStyle(tmpl, regionId, QList<QJsonObject>{overrides}, warnings);
}

} // namespace visualis
