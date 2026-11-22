#pragma once

#include "pco/pcoauth.h"

#include <QString>
#include <optional>

// Where the Planning Center token lives: the macOS Keychain, never the library, settings,
// or logs (Phase 2 acceptance, #52). Other platforms aren't supported yet.
class CredentialStore
{
public:
    static constexpr char kDefaultService[] = "org.visualis.Visualis.planning-center";

    // `service` names the Keychain entry; tests use their own so they never touch the real one.
    explicit CredentialStore(QString service = QString::fromLatin1(kDefaultService)) : m_service(std::move(service)) {}

    static bool isSupported();

    struct Result {
        bool ok = true;
        QString error;   // for the operator
    };
    // Replaces whatever was stored.
    Result save(const visualis::PcoCredentials &credentials) const;
    // Nothing stored (or unreadable): nullopt, with `error` set only for a real failure.
    std::optional<visualis::PcoCredentials> load(QString *error = nullptr) const;
    Result remove() const;

private:
    QString m_service;
};
