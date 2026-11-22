#include "pco/planningcenter.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QSettings>

using namespace visualis;

Q_LOGGING_CATEGORY(lcPco, "visualis.pco")

namespace {
// The organization's name isn't secret; it's kept so the operator sees it after a restart.
constexpr char kOrganizationKey[] = "planningCenter/organization";
}

PlanningCenter::PlanningCenter(QObject *parent)
    : QObject(parent)
{
    setCredentialService(QString::fromLatin1(CredentialStore::kDefaultService));
}

void PlanningCenter::setCredentialService(const QString &service)
{
    m_store = CredentialStore(service);
    QString error;
    m_signedIn = m_store.load(&error).has_value();
    m_organization = m_signedIn ? QSettings().value(kOrganizationKey).toString() : QString();
    m_status = error;
    emit changed();
}

std::unique_ptr<PcoAuthorizer> PlanningCenter::authorizer() const
{
    const std::optional<PcoCredentials> c = m_store.load();
    return c ? std::make_unique<PersonalAccessToken>(*c) : nullptr;
}

void PlanningCenter::signIn(const QString &appId, const QString &secret)
{
    const PcoCredentials c{appId.trimmed(), secret.trimmed()};
    if (!c.isValid()) {
        setStatus(QStringLiteral("Enter both the application ID and the secret."));
        emit signInFinished(false);
        return;
    }
    if (!CredentialStore::isSupported()) {
        setStatus(QStringLiteral("Signing in to Planning Center isn't supported on this system yet."));
        emit signInFinished(false);
        return;
    }
    check(c, true);
}

void PlanningCenter::testConnection()
{
    QString error;
    const std::optional<PcoCredentials> c = m_store.load(&error);
    if (!c) {
        setStatus(error.isEmpty() ? QStringLiteral("Not signed in.") : error);
        return;
    }
    check(*c, false);
}

void PlanningCenter::signOut()
{
    const CredentialStore::Result r = m_store.remove();
    if (!r.ok) {
        setStatus(r.error);
        return;
    }
    QSettings().remove(kOrganizationKey);
    m_signedIn = false;
    m_organization.clear();
    qCInfo(lcPco) << "signed out";
    setStatus(QStringLiteral("Signed out. The token was removed from the Keychain."));
}

void PlanningCenter::check(const PcoCredentials &credentials, bool saveIfAccepted)
{
    QNetworkRequest request(m_baseUrl);
    request.setRawHeader("Authorization", PersonalAccessToken(credentials).authorization());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Visualis/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(15000);
    m_busy = true;
    setStatus(QStringLiteral("Checking with Planning Center…"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, credentials, saveIfAccepted] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const OrganizationResult org = organizationFromResponse(status, reply->readAll());
        m_busy = false;
        if (!org.error.isEmpty()) {
            qCInfo(lcPco) << "check failed, HTTP" << status;   // never the token
            setStatus(org.error);
            emit signInFinished(false);
            return;
        }
        if (saveIfAccepted) {
            if (const CredentialStore::Result saved = m_store.save(credentials); !saved.ok) {
                setStatus(saved.error);
                emit signInFinished(false);
                return;
            }
        }
        m_signedIn = true;
        m_organization = org.name;
        QSettings().setValue(kOrganizationKey, org.name);
        qCInfo(lcPco).noquote() << "connected to" << org.name;
        setStatus(QStringLiteral("Connected to %1.").arg(org.name));
        emit signInFinished(true);
    });
}

void PlanningCenter::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}
