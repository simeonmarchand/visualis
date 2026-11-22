#include "pco/credentialstore.h"

using namespace visualis;

// The OS keychain on Windows (Credential Manager) and Linux (Secret Service) comes when
// those platforms are taken on (D1). Until then nothing is stored, rather than storing it badly.
bool CredentialStore::isSupported()
{
    return false;
}

CredentialStore::Result CredentialStore::save(const PcoCredentials &) const
{
    return {false, QStringLiteral("Saving the Planning Center token isn't supported on this system yet.")};
}

std::optional<PcoCredentials> CredentialStore::load(QString *) const
{
    return std::nullopt;
}

CredentialStore::Result CredentialStore::remove() const
{
    return {};
}
