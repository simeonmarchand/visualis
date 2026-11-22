#pragma once

#include "service/service.h"

#include <QList>
#include <optional>

namespace visualis {

class Library;

struct ServiceSummary {
    QUuid id;
    QString title;
    QDate date;
};

// Reads and writes services in a Library, like SongStore does songs: a service is saved
// whole, items in order, in one transaction.
class ServiceStore
{
public:
    explicit ServiceStore(Library &library);

    struct Result {
        bool ok = true;
        QString error;    // for the operator
    };

    // Saves the service and its items in their current order (reorder by reordering
    // `items` and saving). Gives new services and items ids and sets the timestamps. Items
    // no longer in the list are deleted.
    Result save(Service &service);
    // A song item whose song is no longer in the library comes back as a placeholder with
    // its title, so a service never points at nothing.
    std::optional<Service> load(const QUuid &id) const;
    bool remove(const QUuid &id);
    // Every service, newest date first, then by title.
    QList<ServiceSummary> list() const;
    // The service imported from this Planning Center plan, if any (the first, if several).
    std::optional<ServiceSummary> findByPcoPlan(const QString &pcoPlanId) const;

private:
    Library &m_library;
};

} // namespace visualis
