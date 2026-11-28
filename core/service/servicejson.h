#pragma once

#include "service/service.h"

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <optional>

namespace visualis {

// Services as human-readable JSON (plan 5.9), like songs': for backup, moving between
// libraries, and test fixtures. Items name their song by id; imported into a library that
// lacks the song, the item loads as a placeholder with its title.
//
// {"format": "visualis-service", "version": 1, "id": "…", "title": "Sunday", "date": "2026-11-29",
//  "pcoPlanId": "", "pcoServiceTypeId": "", "pcoPlanUrl": "", "lastImportedAt": "", "defaultBackground": "",
//  "items": [{"id": "…", "kind": "song", "title": "Amazing Grace", "ref": "<song id>",
//             "arrangement": "", "customSequence": ["Verse 1", "Chorus"], "body": "",
//             "servicePosition": "", "pcoItemId": "", "overrides": {}, "importSnapshot": {}}]}
QByteArray serviceToJson(const Service &service);

struct ServiceJsonResult {
    std::optional<Service> service;
    QString error;          // set when the JSON can't be read as a service
    QStringList warnings;   // e.g. an unknown item kind, read as a placeholder
};
ServiceJsonResult serviceFromJson(const QByteArray &json);

// One item, as it appears in "items" above; also kept in the live state journal (#93). An
// unknown kind reads as a placeholder, with a warning.
QJsonObject serviceItemToJson(const ServiceItem &item);
ServiceItem serviceItemFromJson(const QJsonObject &json, QStringList *warnings = nullptr);

} // namespace visualis
