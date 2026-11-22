#include "pco/pcoauth.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace visualis {

QByteArray PersonalAccessToken::authorization() const
{
    const QByteArray pair = m_credentials.appId.trimmed().toUtf8() + ':' + m_credentials.secret.trimmed().toUtf8();
    return "Basic " + pair.toBase64();
}

OrganizationResult organizationFromResponse(int httpStatus, const QByteArray &body)
{
    switch (httpStatus) {
    case 200:
        break;
    case 401:
        return {{}, QStringLiteral("Planning Center didn't accept that application ID and secret.")};
    case 403:
        return {{}, QStringLiteral("That token can't use Planning Center Services. Check its permissions.")};
    case 429:
        return {{}, QStringLiteral("Planning Center is busy (too many requests). Try again in a minute.")};
    case 0:
        return {{}, QStringLiteral("Couldn't reach Planning Center. Check the internet connection.")};
    default:
        return {{}, QStringLiteral("Planning Center answered with an error (%1). Try again later.").arg(httpStatus)};
    }
    const QJsonObject data = QJsonDocument::fromJson(body).object().value("data").toObject();
    const QString name = data.value("attributes").toObject().value("name").toString();
    if (data.value("type").toString() != "Organization" || name.isEmpty())
        return {{}, QStringLiteral("Planning Center's answer wasn't what Visualis expected.")};
    return {name, {}};
}

} // namespace visualis
