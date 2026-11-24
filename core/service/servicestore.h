#pragma once

#include "library/library.h"
#include "service/service.h"

#include <QList>
#include <functional>
#include <optional>

namespace visualis {

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
        QString error;          // for the operator
        bool busy = false;      // saveNewPlan, applyWithoutWaiting: another program holds the write lock
        QString existing;       // saveNewPlan: the title of the service already made from the plan
    };

    // Saves the service and its items in their current order (reorder by reordering
    // `items` and saving). Gives new services and items ids and sets the timestamps. Items
    // no longer in the list are deleted.
    Result save(Service &service);
    // Saves a service imported from a Planning Center plan, unless the library already has
    // one from that plan (then `existing` names it). The check and the save are one
    // transaction. It never waits for the write lock: if another program holds it, it fails
    // at once with `busy` set, so the caller can try again later without blocking (#76).
    // Writes the caller makes in the same transaction, after the service's: an error from it
    // rolls back the lot. Planning Center links on library songs, so they're saved with the
    // service that made them or not at all (#103 review, M6).
    using AlsoWrite = std::function<QString()>;
    Result saveNewPlan(Service &service, const AlsoWrite &also = {});
    // Changes a saved service as one step (#103 review): takes the write lock without waiting
    // (`busy` if another program has it), loads the service under it, and asks `decide` what
    // to save. With no decision, nothing is written and the result isn't ok, with no error;
    // the caller knows why. Otherwise the service is saved with `also`, and `saved` gets it
    // as saved.
    struct Decision {
        Service service;
        AlsoWrite also;
    };
    using Decide = std::function<std::optional<Decision>(const Service &current)>;
    Result applyWithoutWaiting(const QUuid &id, const Decide &decide, Service *saved = nullptr);
    // A song item whose song is no longer in the library comes back as a placeholder with
    // its title, so a service never points at nothing.
    std::optional<Service> load(const QUuid &id) const;
    bool remove(const QUuid &id);
    // Every service, newest date first, then by title.
    QList<ServiceSummary> list() const;
    // The service imported from this Planning Center plan, if any (the first, if several).
    std::optional<ServiceSummary> findByPcoPlan(const QString &pcoPlanId) const;

private:
    Result prepare(Service &service) const;
    QString write(Service &service);
    // write(), then `also`, then commit `tx`. Anything that fails leaves `tx` to roll it all back.
    Result finish(Service &service, Library::Transaction &tx, const AlsoWrite &also);

    Library &m_library;
};

} // namespace visualis
