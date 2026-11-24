#include "templatejson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>

namespace visualis {

namespace {

constexpr int kVersion = 1;
const char *kFormat = "visualis-template";
constexpr int kMaxTransitionMs = 10'000;

// How a value reads in a warning.
QString show(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String: return QStringLiteral("\"%1\"").arg(v.toString().left(40));
    case QJsonValue::Double: return QString::number(v.toDouble());
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Null: return QStringLiteral("null");
    case QJsonValue::Array: return QStringLiteral("a list");
    case QJsonValue::Object: return QStringLiteral("an object");
    case QJsonValue::Undefined: break;
    }
    return QStringLiteral("nothing");
}

// Reads properties of one JSON object onto values that already hold their fallbacks. A
// missing key is silent (nothing to apply); a key with a bad value is reported.
class Reader
{
public:
    Reader(const QJsonObject &o, QString where, QStringList *warnings)
        : m_o(o), m_where(std::move(where)), m_warnings(warnings)
    {
    }

    // Within [min, max], or (min, max] with `minExclusive`.
    void number(const char *key, double min, double max, double &out, bool minExclusive = false)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || !std::isfinite(d) || d < min || d > max || (minExclusive && d <= min))
            return bad(key, v, QString::number(out));
        out = d;
    }

    void integer(const char *key, int min, int max, int &out)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || d != std::floor(d) || d < min || d > max)
            return bad(key, v, QString::number(out));
        out = int(d);
    }

    void boolean(const char *key, bool &out)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isBool())
            return bad(key, v, out ? QStringLiteral("true") : QStringLiteral("false"));
        out = v.toBool();
    }

    void text(const char *key, QString &out, bool allowEmpty = false)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        const QString s = v.toString().trimmed();
        if (!v.isString() || (!allowEmpty && s.isEmpty()) || s.size() > templatelimits::kTextLength)
            return bad(key, v, QStringLiteral("\"%1\"").arg(out));
        out = s;
    }

    void color(const char *key, QString &out)
    {
        static const QRegularExpression hex(QStringLiteral("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isString() || !hex.match(v.toString()).hasMatch())
            return bad(key, v, out);
        out = v.toString().toLower();
    }

    // One of `names`, whose index is the enum value.
    template <typename E>
    void choice(const char *key, const QStringList &names, E &out)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        const qsizetype i = v.isString() ? names.indexOf(v.toString()) : -1;
        if (i < 0)
            return bad(key, v, names.value(int(out)));
        out = E(i);
    }

    // A nested object; reported if the key holds something else.
    QJsonObject object(const char *key)
    {
        const QJsonValue v = m_o.value(QLatin1String(key));
        if (!v.isUndefined() && !v.isObject())
            bad(key, v, QStringLiteral("the default"));
        return v.toObject();
    }

    QString where(const char *key) const { return m_where + QLatin1Char('.') + QLatin1String(key); }

    void report(const QString &warning)
    {
        if (m_warnings)
            *m_warnings << warning;
    }

private:
    void bad(const char *key, const QJsonValue &v, const QString &fallback)
    {
        report(QStringLiteral("%1 is %2, which isn't valid; using %3").arg(where(key), show(v), fallback));
    }

    QJsonObject m_o;
    QString m_where;
    QStringList *m_warnings;
};

const QStringList kHAligns = {"left", "center", "right"};
const QStringList kVAligns = {"top", "center", "bottom"};
const QStringList kTransitionKinds = {"cut", "fade"};

// `known` laid over `kept`, recursing into objects both have, so unknown keys at any depth
// survive a write.
QJsonObject overlay(QJsonObject kept, const QJsonObject &known)
{
    for (auto it = known.begin(); it != known.end(); ++it) {
        const QJsonValue old = kept.value(it.key());
        if (it->isObject() && old.isObject())
            kept[it.key()] = overlay(old.toObject(), it->toObject());
        else
            kept[it.key()] = *it;
    }
    return kept;
}

QJsonObject transitionToJson(const Transition &t)
{
    return {{"kind", kTransitionKinds.value(int(t.kind))}, {"ms", t.ms}};
}

Transition transitionFrom(const QJsonObject &o, const QString &where, QStringList *warnings, Transition t)
{
    Reader r(o, where, warnings);
    const QJsonValue kind = o.value("kind");
    if (!kind.isUndefined()) {
        const qsizetype i = kind.isString() ? kTransitionKinds.indexOf(kind.toString()) : -1;
        t.kind = i < 0 ? TransitionKind::Cut : TransitionKind(i);
        if (i < 0)
            r.report(QStringLiteral("%1 is %2, which this version doesn't know; using a cut").arg(r.where("kind"), show(kind)));
    }
    r.integer("ms", 0, kMaxTransitionMs, t.ms);
    return t;
}

QString time(const QDateTime &t) { return t.toUTC().toString(Qt::ISODateWithMs); }

} // namespace

QJsonObject regionStyleToJson(const RegionStyle &s)
{
    return {
        {"frame", QJsonObject{{"x", s.frame.x()}, {"y", s.frame.y()}, {"w", s.frame.width()}, {"h", s.frame.height()}}},
        {"fontFamily", s.fontFamily},
        {"maxSize", s.maxSize},
        {"minSize", s.minSize},
        {"weight", s.weight},
        {"color", s.color},
        {"align", kHAligns.value(int(s.hAlign))},
        {"verticalAlign", kVAligns.value(int(s.vAlign))},
        {"lineHeight", s.lineHeight},
        {"shadow", QJsonObject{{"enabled", s.shadow.enabled}, {"color", s.shadow.color}, {"opacity", s.shadow.opacity},
                               {"blur", s.shadow.blur}, {"offset", s.shadow.offset}}},
        {"outline", QJsonObject{{"color", s.outline.color}, {"width", s.outline.width}}},
    };
}

RegionStyle applyRegionProperties(const RegionStyle &base, const QJsonObject &json, const QString &where,
                                  QStringList *warnings)
{
    RegionStyle s = base;
    Reader r(json, where, warnings);

    if (const QJsonObject f = r.object("frame"); !f.isEmpty()) {
        Reader fr(f, r.where("frame"), warnings);
        double x = s.frame.x(), y = s.frame.y(), w = s.frame.width(), h = s.frame.height();
        fr.number("x", 0, 1, x);
        fr.number("y", 0, 1, y);
        fr.number("w", 0, 1, w, true);
        fr.number("h", 0, 1, h, true);
        s.frame = QRectF(x, y, w, h);
    }
    r.text("fontFamily", s.fontFamily);
    r.number("maxSize", 0, 1, s.maxSize, true);
    r.number("minSize", 0, 1, s.minSize, true);
    r.integer("weight", 1, 1000, s.weight);
    r.color("color", s.color);
    r.choice("align", kHAligns, s.hAlign);
    r.choice("verticalAlign", kVAligns, s.vAlign);
    r.number("lineHeight", 0, 10, s.lineHeight, true);

    if (const QJsonObject sh = r.object("shadow"); !sh.isEmpty()) {
        Reader sr(sh, r.where("shadow"), warnings);
        sr.boolean("enabled", s.shadow.enabled);
        sr.color("color", s.shadow.color);
        sr.number("opacity", 0, 1, s.shadow.opacity);
        sr.number("blur", 0, 1, s.shadow.blur);
        sr.number("offset", -0.1, 0.1, s.shadow.offset);
    }
    if (const QJsonObject ol = r.object("outline"); !ol.isEmpty()) {
        Reader orr(ol, r.where("outline"), warnings);
        orr.color("color", s.outline.color);
        orr.number("width", 0, 0.1, s.outline.width);
    }
    return s;
}

RegionStyle enforceRegionConstraints(RegionStyle s, const RegionStyle &fallback, const QString &where,
                                     QStringList *warnings)
{
    auto report = [&](const QString &w) {
        if (warnings)
            *warnings << w;
    };
    constexpr double kSlack = 1e-9;   // fractions that add to 1 may round just past it
    if (s.frame.right() > 1 + kSlack || s.frame.bottom() > 1 + kSlack) {
        report(QStringLiteral("%1.frame runs off the output; using the frame it had").arg(where));
        s.frame = fallback.frame;
    }
    if (s.minSize > s.maxSize) {
        report(QStringLiteral("%1.minSize is larger than %1.maxSize; using %1.maxSize for both").arg(where));
        s.minSize = s.maxSize;
    }
    return s;
}

RegionStyle applyRegionStyle(const RegionStyle &base, const QJsonObject &json, const QString &where,
                             QStringList *warnings)
{
    return enforceRegionConstraints(applyRegionProperties(base, json, where, warnings), base, where, warnings);
}

QByteArray templateToJson(const Template &t)
{
    QJsonArray regions;
    for (const TextRegion &region : t.regions) {
        QJsonObject known = regionStyleToJson(region.style);
        known["id"] = region.id;
        known["name"] = region.name;
        regions.append(overlay(region.extra, known));
    }
    QJsonObject o = overlay(t.extra, {
        {"format", kFormat},
        {"version", kVersion},
        {"id", t.id.isNull() ? QJsonValue() : QJsonValue(t.id.toString(QUuid::WithoutBraces))},
        {"name", t.name},
        {"kind", templateKindName(t.kind)},
        {"revision", t.revision},
        {"safeArea", QJsonObject{{"x", t.safeAreaX}, {"y", t.safeAreaY}}},
        {"transitions", QJsonObject{{"content", transitionToJson(t.transitions.content)},
                                    {"background", transitionToJson(t.transitions.background)}}},
        {"regions", regions},
    });
    o.remove("created");
    o.remove("updated");
    if (t.created.isValid())
        o["created"] = time(t.created);
    if (t.updated.isValid())
        o["updated"] = time(t.updated);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

TemplateJsonResult templateFromJson(const QByteArray &json)
{
    TemplateJsonResult result;
    auto fail = [&](const QString &why) {
        result.tmpl.reset();
        result.error = why;
        return result;
    };
    if (json.size() > templatelimits::kBytes)
        return fail(QStringLiteral("too large to be a template (%1 KB; the limit is %2 KB)")
                        .arg(json.size() / 1024).arg(templatelimits::kBytes / 1024));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not valid JSON (%1 at character %2)").arg(parseError.errorString()).arg(parseError.offset));
    if (!doc.isObject())
        return fail(QStringLiteral("not a Visualis template"));
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != kFormat)
        return fail(QStringLiteral("not a Visualis template (format is \"%1\")").arg(o.value("format").toString()));
    const int version = o.value("version").toInt(0);
    if (version < 1)
        return fail(QStringLiteral("the template has no valid version"));
    if (version > kVersion)
        return fail(QStringLiteral("made by a newer Visualis (template format %1; this version reads up to %2)")
                        .arg(version).arg(kVersion));

    QStringList *warnings = &result.warnings;
    Template t;
    t.extra = o;
    Reader r(o, QStringLiteral("template"), warnings);

    if (const QJsonValue id = o.value("id"); !id.isUndefined() && !id.isNull()) {
        t.id = QUuid::fromString(id.toString());
        if (t.id.isNull())
            r.report(QStringLiteral("template.id is %1, which isn't valid; it gets a new id").arg(show(id)));
    }
    t.name = QStringLiteral("Untitled template");
    if (o.value("name").toString().trimmed().isEmpty())
        r.report(QStringLiteral("the template has no name; using \"%1\"").arg(t.name));
    else
        r.text("name", t.name);
    if (const QJsonValue kind = o.value("kind"); !kind.isUndefined()) {
        if (const auto k = templateKindFromName(kind.toString()))
            t.kind = *k;
        else
            r.report(QStringLiteral("template.kind is %1, which isn't valid; using \"lyrics\"").arg(show(kind)));
    }
    r.integer("revision", 0, std::numeric_limits<int>::max(), t.revision);

    if (const QJsonObject sa = r.object("safeArea"); !sa.isEmpty()) {
        Reader sr(sa, r.where("safeArea"), warnings);
        sr.number("x", 0, templatelimits::kMaxSafeArea, t.safeAreaX);
        sr.number("y", 0, templatelimits::kMaxSafeArea, t.safeAreaY);
    }
    if (const QJsonObject tr = r.object("transitions"); !tr.isEmpty()) {
        Reader trr(tr, r.where("transitions"), warnings);
        t.transitions.content = transitionFrom(trr.object("content"), trr.where("content"), warnings, t.transitions.content);
        t.transitions.background =
            transitionFrom(trr.object("background"), trr.where("background"), warnings, t.transitions.background);
    }

    const QJsonValue regionsValue = o.value("regions");
    if (!regionsValue.isUndefined() && !regionsValue.isArray())
        r.report(QStringLiteral("template.regions is %1, which isn't a list; the template has no regions").arg(show(regionsValue)));
    const QJsonArray regions = regionsValue.toArray();
    if (regions.size() > templatelimits::kRegions)
        r.report(QStringLiteral("the template has %1 regions; only the first %2 are kept")
                     .arg(regions.size()).arg(templatelimits::kRegions));
    QSet<QString> ids;
    for (qsizetype i = 0; i < regions.size() && i < templatelimits::kRegions; ++i) {
        const QString where = QStringLiteral("region %1").arg(i + 1);
        if (!regions[i].isObject()) {
            r.report(QStringLiteral("%1 is %2, not a region; skipped").arg(where, show(regions[i])));
            continue;
        }
        const QJsonObject ro = regions[i].toObject();
        TextRegion region;
        region.id = ro.value("id").toString().trimmed();
        if (region.id.isEmpty() || region.id.size() > templatelimits::kTextLength) {
            r.report(QStringLiteral("%1 has no valid id; skipped").arg(where));
            continue;
        }
        if (ids.contains(region.id)) {
            r.report(QStringLiteral("%1 has the id \"%2\", like an earlier region; skipped").arg(where, region.id));
            continue;
        }
        ids.insert(region.id);
        Reader rr(ro, QStringLiteral("region \"%1\"").arg(region.id), warnings);
        rr.text("name", region.name, true);
        region.style = applyRegionStyle(RegionStyle{}, ro, QStringLiteral("region \"%1\"").arg(region.id), warnings);
        region.extra = ro;
        t.regions << region;
    }

    t.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODateWithMs);
    t.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODateWithMs);
    result.tmpl = t;
    return result;
}

} // namespace visualis
