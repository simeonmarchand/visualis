#pragma once

#include "pco/planningcenter.h"

#include <QUuid>

// For tests that load the UI: its PlanningCenter uses a throwaway Keychain entry, never
// the real one (#73). Call before loading any QML.
inline QString useTestKeychainEntry()
{
    const QString service = "org.visualis.tests.ui." + QUuid::createUuid().toString(QUuid::WithoutBraces);
    PlanningCenter::setQmlStore(std::make_shared<CredentialStore>(service));
    return service;
}
