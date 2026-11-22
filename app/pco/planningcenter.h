#pragma once

#include "pco/credentialstore.h"
#include "pco/pcoauth.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The Planning Center connection for the operator (#52): sign in with a Personal Access
// Token, see which organization it belongs to, sign out. The token is kept in the macOS
// Keychain only. Every request is asynchronous, so a slow or dead network never holds up
// the operator window or the outputs; so is every Keychain call, which can wait on a
// prompt or a locked keychain (#73).
class PlanningCenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // A token is saved (it worked when it was saved).
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    // The organization the token belongs to, as of the last successful check.
    Q_PROPERTY(QString organization READ organization NOTIFY changed)
    // What the last action did, for the operator.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    // The real Keychain entry.
    explicit PlanningCenter(QObject *parent = nullptr);
    // Tests pass their own store, so nothing touches the real entry.
    explicit PlanningCenter(std::shared_ptr<const CredentialStore> store, QObject *parent = nullptr);

    bool signedIn() const { return m_signedIn; }
    QString organization() const { return m_organization; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    // Checks the token with Planning Center first and saves it only if it's accepted.
    Q_INVOKABLE void signIn(const QString &appId, const QString &secret);
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void signOut();

    // For the Planning Center client (#53): how to authorize a request, or null when
    // signed out (or the Keychain hasn't been read yet, just after startup). From memory:
    // the Keychain is read once at startup and written on sign-in and sign-out.
    std::unique_ptr<visualis::PcoAuthorizer> authorizer() const;
    QNetworkAccessManager *network() { return &m_network; }
    QUrl baseUrl() const { return m_baseUrl; }

    // For tests: another API address.
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }

signals:
    void changed();
    void signInFinished(bool ok);

private:
    void check(const visualis::PcoCredentials &credentials, bool saveIfAccepted);
    void setStatus(const QString &status);
    // Runs `work(store)` on the Keychain thread, then `done(result)` back on this one.
    template<typename Work, typename Done>
    void onKeychain(Work work, Done done);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl{QString::fromLatin1(visualis::kPcoServicesApi)};
    std::shared_ptr<const CredentialStore> m_store;
    std::optional<visualis::PcoCredentials> m_credentials;   // as saved in the Keychain
    QString m_loadError;
    bool m_signedIn = false;
    QString m_organization;
    QString m_status;
    bool m_busy = false;
    // One thread, so Keychain calls run in the order made (sign in, then sign out). Last, so
    // it's destroyed first and waits for a running call before the rest goes.
    QThreadPool m_keychain;
};
