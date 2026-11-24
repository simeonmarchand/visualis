#include "overrides.h"

#include "templatejson.h"

#include <algorithm>

namespace visualis::overrides {

namespace {

// Every region property's path, from the template JSON's own keys.
QStringList pathsIn(const QJsonObject &o, const QString &prefix = {})
{
    QStringList out;
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString path = prefix + it.key();
        if (it->isObject())
            out << pathsIn(it->toObject(), path + QLatin1Char('.'));
        else
            out << path;
    }
    return out;
}

// `value` at `keys` inside `o`, keeping everything else there.
QJsonObject withValue(QJsonObject o, QStringList keys, const QJsonValue &value)
{
    const QString key = keys.takeFirst();
    o[key] = keys.isEmpty() ? value : QJsonValue(withValue(o.value(key).toObject(), keys, value));
    return o;
}

// `o` without the value at `keys`, and without objects that leaves empty.
QJsonObject without(QJsonObject o, QStringList keys)
{
    const QString key = keys.takeFirst();
    if (keys.isEmpty()) {
        o.remove(key);
        return o;
    }
    const QJsonValue inner = o.value(key);
    if (!inner.isObject())
        return o;
    const QJsonObject rest = without(inner.toObject(), keys);
    if (rest.isEmpty())
        o.remove(key);
    else
        o[key] = rest;
    return o;
}

} // namespace

const QStringList &regionProperties()
{
    static const QStringList paths = [] {
        QStringList p = pathsIn(regionStyleToJson(RegionStyle{}));
        std::sort(p.begin(), p.end());
        return p;
    }();
    return paths;
}

QJsonValue propertyValue(const QJsonObject &region, const QString &path)
{
    QJsonValue v = region;
    for (const QString &key : path.split(QLatin1Char('.')))
        v = v.toObject().value(key);
    return v;
}

QStringList overriddenProperties(const QJsonObject &overrides, const QString &regionId)
{
    const QJsonObject region = overrides.value("regions").toObject().value(regionId).toObject();
    QStringList out;
    for (const QString &path : pathsIn(region)) {
        if (regionProperties().contains(path))
            out << path;
    }
    std::sort(out.begin(), out.end());
    return out;
}

int propertyCount(const QJsonObject &overrides)
{
    int n = 0;
    const QJsonObject regions = overrides.value("regions").toObject();
    for (auto it = regions.begin(); it != regions.end(); ++it)
        n += overriddenProperties(overrides, it.key()).size();
    return n;
}

QString setProperty(QJsonObject &overrides, const QString &regionId, const QString &path, const QJsonValue &value)
{
    if (regionId.trimmed().isEmpty() || regionId.size() > templatelimits::kTextLength)
        return QStringLiteral("\"%1\" isn't a region id").arg(regionId.left(40));
    if (!regionProperties().contains(path))
        return QStringLiteral("\"%1\" isn't a template property").arg(path.left(40));
    const QStringList keys = path.split(QLatin1Char('.'));
    QStringList warnings;
    applyRegionProperties(RegionStyle{}, withValue({}, keys, value), QStringLiteral("override of \"%1\"").arg(regionId),
                          &warnings);
    if (!warnings.isEmpty())
        return warnings.first();
    overrides = withValue(overrides, QStringList{QStringLiteral("regions"), regionId} + keys, value);
    return {};
}

void resetProperty(QJsonObject &overrides, const QString &regionId, const QString &path)
{
    overrides = without(overrides, QStringList{QStringLiteral("regions"), regionId} + path.split(QLatin1Char('.')));
}

void resetAll(QJsonObject &overrides)
{
    overrides.remove("regions");
    overrides.remove("template");
}

void setTemplate(QJsonObject &overrides, const QUuid &templateId)
{
    if (templateId.isNull())
        overrides.remove("template");
    else
        overrides["template"] = templateId.toString(QUuid::WithoutBraces);
}

} // namespace visualis::overrides
