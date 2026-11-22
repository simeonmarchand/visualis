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

namespace {
std::shared_ptr<const CredentialStore> &qmlStore()
{
    static std::shared_ptr<const CredentialStore> store;
    return store;
}
} // namespace

PlanningCenter *PlanningCenter::create(QQmlEngine *, QJSEngine *)
{
    if (!qmlStore())
        qmlStore() = std::make_shared<CredentialStore>(QString::fromLatin1(CredentialStore::kDefaultService));
    return new PlanningCenter(qmlStore());
}

void PlanningCenter::setQmlStore(std::shared_ptr<const CredentialStore> store)
{
    qmlStore() = std::move(store);
}

PlanningCenter::PlanningCenter(std::shared_ptr<const CredentialStore> store, QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
{
    m_keychain.setMaxThreadCount(1);
    struct Loaded {
        std::optional<PcoCredentials> credentials;
        QString error;
    };
    onKeychain([](const CredentialStore &s) {
        Loaded l;
        l.credentials = s.load(&l.error);
        return l;
    }, [this](const Loaded &l) {
        m_credentials = l.credentials;
        m_loadError = l.error;
        m_signedIn = l.credentials.has_value();
        m_organization = m_signedIn ? QSettings().value(kOrganizationKey).toString() : QString();
        // Only a failure: the operator may already have started something with its own status.
        if (l.error.isEmpty())
            emit changed();
        else
            setStatus(l.error);
    });
}

template<typename Work, typename Done>
void PlanningCenter::onKeychain(Work work, Done done)
{
    m_keychain.start([this, store = m_store, work, done] {
        auto result = work(*store);
        // Queued to this object: dropped if it's gone by then.
        QMetaObject::invokeMethod(this, [done, result] { done(result); }, Qt::QueuedConnection);
    });
}

std::unique_ptr<PcoAuthorizer> PlanningCenter::authorizer() const
{
    return m_credentials ? std::make_unique<PersonalAccessToken>(*m_credentials) : nullptr;
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
    if (!m_credentials) {
        setStatus(m_loadError.isEmpty() ? QStringLiteral("Not signed in.") : m_loadError);
        return;
    }
    check(*m_credentials, false);
}

void PlanningCenter::signOut()
{
    onKeychain([](const CredentialStore &s) { return s.remove(); }, [this](const CredentialStore::Result &r) {
        if (!r.ok) {
            setStatus(r.error);
            return;
        }
        QSettings().remove(kOrganizationKey);
        m_credentials.reset();
        m_loadError.clear();
        m_signedIn = false;
        m_organization.clear();
        qCInfo(lcPco) << "signed out";
        setStatus(QStringLiteral("Signed out. The token was removed from the Keychain."));
    });
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
        if (!org.error.isEmpty()) {
            qCInfo(lcPco) << "check failed, HTTP" << status;   // never the token
            m_busy = false;
            setStatus(org.error);
            emit signInFinished(false);
            return;
        }
        const auto connected = [this, credentials, name = org.name] {
            m_busy = false;
            m_credentials = credentials;
            m_loadError.clear();
            m_signedIn = true;
            m_organization = name;
            QSettings().setValue(kOrganizationKey, name);
            qCInfo(lcPco).noquote() << "connected to" << name;
            setStatus(QStringLiteral("Connected to %1.").arg(name));
            emit signInFinished(true);
        };
        if (!saveIfAccepted) {
            connected();
            return;
        }
        onKeychain([credentials](const CredentialStore &s) { return s.save(credentials); },
                   [this, connected](const CredentialStore::Result &saved) {
            if (!saved.ok) {
                // Whatever was signed in before still is: the store kept it.
                m_busy = false;
                setStatus(saved.error);
                emit signInFinished(false);
                return;
            }
            connected();
        });
    });
}

void PlanningCenter::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}
