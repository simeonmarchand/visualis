#pragma once

#include <QByteArray>
#include <QString>

namespace visualis {

// Planning Center's Services API (plan 5.3).
inline constexpr char kPcoServicesApi[] = "https://api.planningcenteronline.com/services/v2/";

// How requests to Planning Center are authorized. A Personal Access Token for now;
// OAuth for other churches (plan 7.5) slots in as another implementation, so the client
// and import never change.
class PcoAuthorizer
{
public:
    virtual ~PcoAuthorizer() = default;
    // The Authorization header's value.
    virtual QByteArray authorization() const = 0;
};

struct PcoCredentials
{
    QString appId;
    QString secret;

    bool isValid() const { return !appId.trimmed().isEmpty() && !secret.trimmed().isEmpty(); }
    bool operator==(const PcoCredentials &) const = default;
};

// A Personal Access Token: HTTP Basic with the application ID and secret.
class PersonalAccessToken : public PcoAuthorizer
{
public:
    explicit PersonalAccessToken(PcoCredentials credentials) : m_credentials(std::move(credentials)) {}
    QByteArray authorization() const override;

private:
    PcoCredentials m_credentials;
};

// What the Services API root says about a token: the organization it belongs to (a token
// only sees the organization it was made in; P3 finding 1), or why it didn't work.
struct OrganizationResult
{
    QString name;    // set on success
    QString error;   // for the operator
};
OrganizationResult organizationFromResponse(int httpStatus, const QByteArray &body);

} // namespace visualis
