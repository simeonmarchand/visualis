#include "service/service.h"

#include <array>
#include <utility>

namespace visualis {

namespace {
using K = ServiceItem::Kind;
constexpr std::array<std::pair<K, const char *>, 7> kNames{{
    {K::Header, "header"}, {K::Song, "song"}, {K::Scripture, "scripture"}, {K::Presentation, "presentation"},
    {K::Media, "media"}, {K::Text, "text"}, {K::Placeholder, "placeholder"},
}};
} // namespace

QString kindName(ServiceItem::Kind kind)
{
    for (const auto &[k, name] : kNames) {
        if (k == kind)
            return QString::fromLatin1(name);
    }
    return QStringLiteral("placeholder");
}

std::optional<ServiceItem::Kind> kindFromName(const QString &name)
{
    for (const auto &[k, n] : kNames) {
        if (name == QLatin1String(n))
            return k;
    }
    return std::nullopt;
}

} // namespace visualis
