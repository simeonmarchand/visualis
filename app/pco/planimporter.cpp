#include "pco/planimporter.h"

#include "library/songlibrary.h"
#include "pco/pcoclient.h"
#include "pco/planningcenter.h"
#include "pco/reimport.h"
#include "pco/songlink.h"
#include "service/servicestore.h"
#include "song/songstore.h"

#include <QDate>
#include <QDateTime>
#include <QTimer>

using namespace visualis;

PlanImporter::PlanImporter(QObject *parent)
    : QObject(parent)
{
}

PlanImporter::~PlanImporter() = default;

void PlanImporter::setPlanningCenter(PlanningCenter *pco)
{
    m_pco = pco;
}

void PlanImporter::setSongLibrary(SongLibrary *songs)
{
    if (m_songs)
        disconnect(m_songs, nullptr, this, nullptr);
    m_songs = songs;
    if (m_songs)
        connect(m_songs, &SongLibrary::libraryChanged, this, [this] { ++m_libraryChanges; });
}

void PlanImporter::setClient(std::unique_ptr<PcoClient> client)
{
    m_client = std::move(client);
}

PcoClient *PlanImporter::client()
{
    if (!m_client && m_pco) {
        QPointer<PlanningCenter> pco = m_pco;
        m_client = std::make_unique<PcoClient>(pco->network(), pco->baseUrl(), [pco]() -> std::unique_ptr<PcoAuthorizer> {
            return pco ? pco->authorizer() : nullptr;
        });
    }
    return m_client.get();
}

QVariantList PlanImporter::serviceTypes() const
{
    QVariantList out;
    for (const PcoServiceType &t : m_types)
        out << QVariantMap{{"id", t.id}, {"name", t.name}};
    return out;
}

QVariantList PlanImporter::plans() const
{
    QVariantList out;
    for (const PcoPlan &p : m_plans) {
        const QString title = !p.title.trimmed().isEmpty() ? p.title.trimmed() : p.seriesTitle.trimmed();
        out << QVariantMap{{"id", p.id}, {"label", title.isEmpty() ? p.dates : p.dates + QStringLiteral(" · ") + title},
                           {"dates", p.dates}, {"title", title}, {"itemsCount", p.itemsCount},
                           {"url", p.planningCenterUrl.toString()}};
    }
    return out;
}

void PlanImporter::begin()
{
    if (m_pending++ == 0)
        emit busyChanged();
}

void PlanImporter::end()
{
    if (--m_pending == 0)
        emit busyChanged();
}

void PlanImporter::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void PlanImporter::loadServiceTypes()
{
    PcoClient *pco = client();
    if (!pco) {
        setStatus(tr("Sign in to Planning Center first."));
        return;
    }
    begin();
    setStatus(tr("Reading service types…"));
    pco->serviceTypes([this](PcoResult<QList<PcoServiceType>> r) {
        end();
        if (!r.ok()) {
            setStatus(r.error.message);
            return;
        }
        m_types = r.value;
        emit serviceTypesChanged();
        setStatus(m_types.isEmpty() ? tr("This Planning Center account has no service types.") : QString());
    });
}

void PlanImporter::loadPlans(const QString &serviceTypeId)
{
    PcoClient *pco = client();
    if (!pco) {
        setStatus(tr("Sign in to Planning Center first."));
        return;
    }
    const int request = ++m_plansRequest;
    m_plans.clear();
    emit plansChanged();
    begin();
    setStatus(tr("Reading plans…"));
    pco->futurePlans(serviceTypeId, [this, request](PcoResult<QList<PcoPlan>> r) {
        end();
        if (request != m_plansRequest)
            return;   // the operator picked another type (or the same one again) meanwhile
        if (!r.ok()) {
            setStatus(r.error.message);
            return;
        }
        m_plans = plansForPicker(r.value, QDate::currentDate());
        emit plansChanged();
        setStatus(m_plans.isEmpty() ? tr("No upcoming plans for this service type.") : QString());
    });
}

void PlanImporter::importPlan(const QString &serviceTypeId, const QString &planId)
{
    PcoClient *pco = client();
    if (!pco) {
        setStatus(tr("Sign in to Planning Center first."));
        return;
    }
    if (!m_songs || !m_songs->library()) {
        setStatus(tr("The library isn't open, so there's nowhere to save the service."));
        return;
    }
    if (const auto existing = ServiceStore(*m_songs->library()).findByPcoPlan(planId)) {
        reimport(existing->id.toString(QUuid::WithoutBraces));   // update it, keeping the operator's work
        return;
    }

    PcoServiceType type{serviceTypeId, {}};
    for (const PcoServiceType &t : std::as_const(m_types)) {
        if (t.id == serviceTypeId)
            type = t;
    }
    begin();
    setStatus(tr("Importing…"));
    // Both reads, then one save: a failure part way leaves the library untouched.
    QPointer<PlanImporter> self(this);
    const int library = m_libraryChanges;
    pco->plan(serviceTypeId, planId, [self, pco, type, serviceTypeId, planId, library](PcoResult<PcoPlan> plan) {
        if (!self)
            return;
        if (!plan.ok()) {
            self->end();
            self->setStatus(plan.error.message);
            return;
        }
        pco->planItems(serviceTypeId, planId, [self, type, plan = plan.value, library](PcoResult<PcoPlanItems> items) {
            if (!self)
                return;
            self->end();
            if (!items.ok()) {
                self->setStatus(items.error.message);
                return;
            }
            self->save(type, plan, items.value, library);
        });
    });
}

void PlanImporter::save(const PcoServiceType &type, const PcoPlan &plan, const PcoPlanItems &items, int library)
{
    PlanImport r = serviceFromPlan(type, plan, items, QDateTime::currentDateTimeUtc());
    if (r.service.items.isEmpty()) {
        // Nothing to run, and saving it would block a retry (a plan is imported once).
        QString why = tr("Nothing was imported: Planning Center sent no items for this plan that could be read.");
        if (!r.notes.isEmpty())
            why += QLatin1Char(' ') + r.notes.join(QLatin1Char(' '));
        setStatus(why);
        return;
    }
    // Songs the library is sure of become song items (#55); the rest wait for the operator.
    // Reading songs never waits on another program's write lock (WAL), so it's done once,
    // before the first attempt to save.
    // trySave() reports a closed or switched library; link only into the one imported into.
    SongLinkReport links;
    if (library == m_libraryChanges && m_songs && m_songs->library()) {
        SongStore songStore(*m_songs->library());
        links = linkSongs(r.service, songStore);
    }
    begin();
    trySave(std::move(r), std::move(links), QDeadlineTimer(m_lockWaitLimitMs), library);
}

void PlanImporter::trySave(PlanImport r, SongLinkReport links, QDeadlineTimer deadline, int library)
{
    if (library != m_libraryChanges) {
        end();
        setStatus(tr("\"%1\" wasn't imported: another library was opened while it was being imported. Import "
                     "it again to put it in this one.").arg(r.service.title));
        return;
    }
    if (!m_songs || !m_songs->library()) {
        end();
        setStatus(tr("The library isn't open, so there's nowhere to save the service."));
        return;
    }
    // The duplicate-plan check is part of the save's transaction: the same plan may have
    // been imported while this one was reading.
    const ServiceStore::Result saved = ServiceStore(*m_songs->library()).saveNewPlan(r.service);
    if (saved.busy) {
        // Another program has the library locked. Try again shortly rather than wait here,
        // which would hold up the operator window and the outputs; never past the deadline.
        const qint64 left = deadline.remainingTime();
        if (left <= 0)
            return gaveUpWaiting(r.service.title);
        setStatus(tr("Waiting for the library: another program is using it…"));
        QTimer::singleShot(int(qMin<qint64>(kLockRetryMs, left)), this,
                           [this, r = std::move(r), links = std::move(links), deadline, library]() mutable {
                               if (deadline.hasExpired())
                                   gaveUpWaiting(r.service.title);
                               else
                                   trySave(std::move(r), std::move(links), deadline, library);
                           });
        return;
    }
    end();
    if (!saved.ok) {
        setStatus(saved.error);
        return;
    }
    const qsizetype n = r.service.items.size();
    QString status = n == 1 ? tr("Imported \"%1\": 1 item.").arg(r.service.title)
                            : tr("Imported \"%1\": %2 items.").arg(r.service.title, QString::number(n));
    if (const QString songs = links.summary(); !songs.isEmpty())
        status += QLatin1Char(' ') + songs;
    for (const QString &missing : links.unmatched)
        status += QLatin1Char(' ') + tr("No group for %1.").arg(missing);
    if (!r.notes.isEmpty())
        status += QLatin1Char(' ') + r.notes.join(QLatin1Char(' '));
    setStatus(status);
    emit imported(r.service.id.toString(QUuid::WithoutBraces), r.service.title);
}

void PlanImporter::gaveUpWaiting(const QString &title)
{
    end();
    setStatus(tr("Couldn't save \"%1\": another program kept the library busy. Nothing was imported; try again.")
                  .arg(title));
}

namespace {

QString changeKindName(ReimportChange::Kind k)
{
    using K = ReimportChange::Kind;
    switch (k) {
    case K::Added: return QStringLiteral("added");
    case K::Removed: return QStringLiteral("removed");
    case K::Restored: return QStringLiteral("restored");
    case K::Moved: return QStringLiteral("moved");
    case K::Changed: return QStringLiteral("changed");
    case K::KeptEdit: return QStringLiteral("keptEdit");
    case K::KeptPosition: return QStringLiteral("keptPosition");
    case K::Relink: return QStringLiteral("relink");
    case K::ChangedButDeleted: return QStringLiteral("changedButDeleted");
    }
    return {};
}

} // namespace

QString PlanImporter::reimportServiceId() const
{
    return m_reimport ? m_reimport->serviceId.toString(QUuid::WithoutBraces) : QString();
}

QVariantList PlanImporter::reimportChanges() const { return m_reimport ? m_reimport->changes : QVariantList(); }

QString PlanImporter::reimportSummary() const { return m_reimport ? m_reimport->summary : QString(); }

void PlanImporter::reimport(const QString &serviceId)
{
    PcoClient *pco = client();
    if (!pco) {
        setStatus(tr("Sign in to Planning Center first."));
        return;
    }
    if (!m_songs || !m_songs->library()) {
        setStatus(tr("The library isn't open."));
        return;
    }
    const QUuid id = QUuid::fromString(serviceId);
    const std::optional<Service> service = ServiceStore(*m_songs->library()).load(id);
    if (!service || service->pcoPlanId.isEmpty()) {
        setStatus(tr("This service wasn't imported from Planning Center."));
        return;
    }
    cancelReimport();
    const int request = ++m_reimportRequest;
    const QString typeId = service->pcoServiceTypeId;
    const QString planId = service->pcoPlanId;
    begin();
    setStatus(tr("Reading \"%1\" from Planning Center…").arg(service->title));
    QPointer<PlanImporter> self(this);
    pco->plan(typeId, planId, [self, pco, id, typeId, planId, request](PcoResult<PcoPlan> plan) {
        if (!self)
            return;
        if (!plan.ok()) {
            self->end();
            self->setStatus(plan.error.message);
            return;
        }
        pco->planItems(typeId, planId, [self, id, typeId, request, plan = plan.value](PcoResult<PcoPlanItems> items) {
            if (!self)
                return;
            self->end();
            if (request != self->m_reimportRequest)
                return;   // cancelled, or asked again since
            if (!items.ok()) {
                self->setStatus(items.error.message);
                return;
            }
            const PlanImport fresh = serviceFromPlan({typeId, {}}, plan, items.value, QDateTime::currentDateTimeUtc());
            if (fresh.service.items.isEmpty()) {
                // Most likely a read that went wrong: merging it would flag every item removed.
                self->setStatus(tr("Planning Center sent no items for this plan that could be read, so nothing "
                                   "was changed."));
                return;
            }
            self->offerReimport(id, fresh.service);
        });
    });
}

void PlanImporter::offerReimport(const QUuid &serviceId, const Service &fresh)
{
    const std::optional<Service> local = m_songs && m_songs->library()
                                             ? ServiceStore(*m_songs->library()).load(serviceId) : std::nullopt;
    if (!local) {
        setStatus(tr("The service is gone from the library."));
        return;
    }
    const ReimportResult r = mergeReimport(*local, fresh);
    PendingReimport p{serviceId, fresh, {}, r.summary()};
    for (const ReimportChange &c : r.changes) {
        p.changes.append(QVariantMap{{"kind", changeKindName(c.kind)}, {"title", c.title}, {"field", c.field},
                                     {"detail", c.detail}, {"pcoItemId", c.pcoItemId}});
    }
    m_reimport = p;
    setStatus(r.summary());
    emit reimportChanged();
}

bool PlanImporter::applyReimport(const QStringList &addBack)
{
    if (!m_reimport || !m_songs || !m_songs->library())
        return false;
    ServiceStore store(*m_songs->library());
    // Against the service as it is now, in case the operator changed it meanwhile.
    const std::optional<Service> local = store.load(m_reimport->serviceId);
    if (!local) {
        setStatus(tr("The service is gone from the library."));
        cancelReimport();
        return false;
    }
    ReimportResult r = mergeReimport(*local, m_reimport->fresh, QSet<QString>(addBack.cbegin(), addBack.cend()));
    SongStore songs(*m_songs->library());
    const SongLinkReport links = linkSongs(r.service, songs);
    if (const ServiceStore::Result saved = store.save(r.service); !saved.ok) {
        setStatus(saved.error);
        return false;
    }
    QString status = tr("Updated \"%1\" from Planning Center. %2").arg(r.service.title, r.summary());
    if (const QString s = links.summary(); !s.isEmpty())
        status += QLatin1Char(' ') + s;
    for (const QString &missing : links.unmatched)
        status += QLatin1Char(' ') + tr("No group for %1.").arg(missing);
    m_reimport.reset();
    emit reimportChanged();
    setStatus(status);
    emit reimported(r.service.id.toString(QUuid::WithoutBraces));
    return true;
}

void PlanImporter::cancelReimport()
{
    ++m_reimportRequest;   // an answer still on its way is dropped
    if (!m_reimport)
        return;
    m_reimport.reset();
    emit reimportChanged();
}
