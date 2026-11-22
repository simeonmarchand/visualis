#pragma once

#include "pco/pcoauth.h"

#include <QString>
#include <optional>

// Where the Planning Center token lives: the macOS Keychain, never the library, settings,
// or logs (Phase 2 acceptance, #52). Other platforms aren't supported yet.
//
// Calls can block (a Keychain prompt, a locked keychain), so keep them off the GUI thread.
// Virtual so tests can stand in a store that fails.
class CredentialStore
{
public:
    static constexpr char kDefaultService[] = "org.visualis.Visualis.planning-center";

    // `service` names the Keychain entry; tests use their own so they never touch the real one.
    explicit CredentialStore(QString service) : m_service(std::move(service)) {}
    virtual ~CredentialStore() = default;

    QString service() const { return m_service; }

    static bool isSupported();

    struct Result {
        bool ok = true;
        QString error;   // for the operator
    };
    // Replaces whatever was stored. If it fails, what was stored stays.
    virtual Result save(const visualis::PcoCredentials &credentials) const;
    // Nothing stored (or unreadable): nullopt, with `error` set only for a real failure.
    virtual std::optional<visualis::PcoCredentials> load(QString *error = nullptr) const;
    virtual Result remove() const;

private:
    QString m_service;
};
