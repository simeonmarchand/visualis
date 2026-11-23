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
    // The songs' Planning Center links are saved after the service, never before it: a
    // service that isn't saved leaves the library as it was.
    SongLinkReport links;
    QList<Song> linked;
    if (library == m_libraryChanges && m_songs && m_songs->library()) {
        SongStore songStore(*m_songs->library());
        links = linkSongs(r.service, songStore, &linked);
    }
    begin();
    const QString title = r.service.title;
    whileBusy(QDeadlineTimer(m_lockWaitLimitMs), [this, r, links, linked, library] { return trySave(r, links, linked, library); },
              [this, title] {
                  end();
                  setStatus(tr("Couldn't save \"%1\": another program kept the library busy. Nothing was imported; "
                               "try again.").arg(title));
              });
}

void PlanImporter::whileBusy(QDeadlineTimer deadline, const std::function<bool()> &attempt,
                             const std::function<void()> &gaveUp)
{
    if (attempt())
        return;
    // Another program has the library locked. Try again shortly rather than wait here, which
    // would hold up the operator window and the outputs; never past the deadline.
    const qint64 left = deadline.remainingTime();
    if (left <= 0)
        return gaveUp();
    setStatus(tr("Waiting for the library: another program is using it…"));
    QTimer::singleShot(int(qMin<qint64>(kLockRetryMs, left)), this, [this, deadline, attempt, gaveUp] {
        if (deadline.hasExpired())
            gaveUp();
        else
            whileBusy(deadline, attempt, gaveUp);
    });
}

bool PlanImporter::trySave(const PlanImport &r, const SongLinkReport &links, const QList<Song> &linked, int library)
{
    if (library != m_libraryChanges) {
        end();
        setStatus(tr("\"%1\" wasn't imported: another library was opened while it was being imported. Import "
                     "it again to put it in this one.").arg(r.service.title));
        return true;
    }
    if (!m_songs || !m_songs->library()) {
        end();
        setStatus(tr("The library isn't open, so there's nowhere to save the service."));
        return true;
    }
    // The duplicate-plan check is part of the save's transaction: the same plan may have
    // been imported while this one was reading.
    QStringList unlinkedSongs;
    Service service = r.service;
    const ServiceStore::Result saved =
        ServiceStore(*m_songs->library()).saveNewPlan(service, linkWrites(linked, &unlinkedSongs));
    if (saved.busy)
        return false;
    end();
    if (!saved.ok) {
        setStatus(saved.error);
        return true;
    }
    const QString unlinked = unlinkedSongs.isEmpty() ? QString()
        : tr("Couldn't remember the Planning Center link for %1.").arg(unlinkedSongs.join(QStringLiteral(", ")));
    const qsizetype n = service.items.size();
    QString status = n == 1 ? tr("Imported \"%1\": 1 item.").arg(service.title)
                            : tr("Imported \"%1\": %2 items.").arg(service.title, QString::number(n));
    if (const QString songs = links.summary(); !songs.isEmpty())
        status += QLatin1Char(' ') + songs;
    for (const QString &missing : links.unmatched)
        status += QLatin1Char(' ') + tr("No group for %1.").arg(missing);
    if (!r.notes.isEmpty())
        status += QLatin1Char(' ') + r.notes.join(QLatin1Char(' '));
    if (!unlinked.isEmpty())
        status += QLatin1Char(' ') + unlinked;
    setStatus(status);
    emit imported(service.id.toString(QUuid::WithoutBraces), service.title);
    return true;
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
    case K::Unreadable: return QStringLiteral("unreadable");
    case K::Linked: return QStringLiteral("linked");
    }
    return {};
}

} // namespace

QString PlanImporter::reimportServiceId() const
{
    return m_reimport ? m_reimport->serviceId.toString(QUuid::WithoutBraces) : QString();
}

QVariantList PlanImporter::reimportChanges() const
{
    QVariantList list;
    if (!m_reimport)
        return list;
    for (const ReimportChange &c : m_reimport->shown.changes) {
        list.append(QVariantMap{{"kind", changeKindName(c.kind)}, {"title", c.title}, {"field", c.field},
                                {"detail", c.detail}, {"pcoItemId", c.pcoItemId}});
    }
    return list;
}

QString PlanImporter::reimportSummary() const { return m_reimport ? m_reimport->shown.summary() : QString(); }

bool PlanImporter::reimportNeedsSave() const { return m_reimport && m_reimport->shown.needsSave(); }

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
    const int library = m_libraryChanges;
    const QString typeId = service->pcoServiceTypeId;
    const QString planId = service->pcoPlanId;
    begin();
    setStatus(tr("Reading \"%1\" from Planning Center…").arg(service->title));
    QPointer<PlanImporter> self(this);
    pco->plan(typeId, planId, [self, pco, id, typeId, planId, request, library](PcoResult<PcoPlan> plan) {
        if (!self)
            return;
        if (!plan.ok()) {
            self->end();
            self->setStatus(plan.error.message);
            return;
        }
        pco->planItems(typeId, planId, [self, id, typeId, request, library, plan = plan.value](PcoResult<PcoPlanItems> items) {
            if (!self)
                return;
            self->end();
            if (request != self->m_reimportRequest)
                return;   // cancelled, or asked again since
            if (library != self->m_libraryChanges) {
                self->setStatus(tr("Another library was opened while the plan was being read. Update the "
                                   "service again from that library."));
                return;
            }
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
            self->offerReimport(id, fresh, library);
        });
    });
}

void PlanImporter::offerReimport(const QUuid &serviceId, const PlanImport &fresh, int library,
                                 const QSet<QString> &addBack, const QString &notice)
{
    const std::optional<Service> local = m_songs && m_songs->library()
                                             ? ServiceStore(*m_songs->library()).load(serviceId) : std::nullopt;
    if (!local) {
        setStatus(tr("The service is gone from the library."));
        return;
    }
    SongStore songs(*m_songs->library());
    m_reimport = PendingReimport{serviceId, fresh, library, addBack, proposeReimport(*local, fresh, addBack, songs), notice};
    setStatus(notice.isEmpty() ? reimportSummary() : notice);
    emit reimportChanged();
}

void PlanImporter::setReimportAddBack(const QStringList &pcoItemIds)
{
    if (!m_reimport)
        return;
    const PendingReimport p = *m_reimport;
    offerReimport(p.serviceId, p.fresh, p.library, QSet<QString>(pcoItemIds.cbegin(), pcoItemIds.cend()), p.notice);
}

bool PlanImporter::applyReimport()
{
    if (!m_reimport)
        return false;
    begin();
    const int request = m_reimportRequest;
    whileBusy(QDeadlineTimer(m_lockWaitLimitMs), [this, request] { return tryApply(request); },
              [this, request] {
                  end();
                  if (!m_reimport || request != m_reimportRequest)
                      return;   // cancelled meanwhile
                  m_reimport.reset();
                  emit reimportChanged();
                  setStatus(tr("Couldn't update the service: another program kept the library busy. Nothing was "
                               "changed; try again."));
              });
    return true;
}

bool PlanImporter::tryApply(int request)
{
    const auto stop = [this](const QString &status) {
        end();
        setStatus(status);
        if (m_reimport) {
            m_reimport.reset();
            emit reimportChanged();
        }
        return true;
    };
    if (!m_reimport || request != m_reimportRequest) {
        end();   // cancelled, or another re-import started
        return true;
    }
    if (m_reimport->library != m_libraryChanges || !m_songs || !m_songs->library())
        return stop(tr("Nothing was updated: another library was opened. Update the service again from that library."));

    // The diff is worked out again under the write lock, from the service as saved: only if
    // it's the one the operator saw is it saved.
    ServiceStore store(*m_songs->library());
    SongStore songs(*m_songs->library());
    const PendingReimport pending = *m_reimport;
    std::optional<ReimportProposal> changed;
    ReimportProposal applied;
    QStringList missing;
    Service saved;
    const ServiceStore::Result result = store.applyWithoutWaiting(
        pending.serviceId,
        [&](const Service &current) -> ServiceStore::Decision {
            ReimportProposal now = proposeReimport(current, pending.fresh, pending.addBack, songs);
            if (now != pending.shown) {
                changed = std::move(now);
                return {std::nullopt, {}, tr("The service changed since the update was shown.")};
            }
            applied = std::move(now);
            return {applied.merge.service, linkWrites(applied.linked, &missing), {}};
        },
        &saved);
    if (result.busy)
        return false;
    if (result.conflict && changed) {
        // Nothing saved: show what applying would do now, for the operator to approve again.
        end();
        m_reimport->shown = std::move(*changed);
        m_reimport->notice = tr("The service changed while the update was open. This is what applying would do now.");
        setStatus(m_reimport->notice);
        emit reimportChanged();
        return true;
    }
    if (!result.ok)
        return stop(result.error);
    QString status = tr("Updated \"%1\" from Planning Center. %2").arg(saved.title, applied.summary());
    for (const QString &gap : applied.links.unmatched)
        status += QLatin1Char(' ') + tr("No group for %1.").arg(gap);
    if (!missing.isEmpty())
        status += QLatin1Char(' ')
                  + tr("Couldn't remember the Planning Center link for %1: it's gone from the library.")
                        .arg(missing.join(QStringLiteral(", ")));
    stop(status);
    emit reimported(saved.id.toString(QUuid::WithoutBraces));
    return true;
}

std::function<QString()> PlanImporter::linkWrites(const QList<Song> &linked, QStringList *missing)
{
    if (linked.isEmpty() || !m_songs || !m_songs->library())
        return {};
    Library *library = m_songs->library();
    return [library, linked, missing] {
        SongStore songs(*library);
        for (const Song &song : linked) {
            // Only a song that's gone is skipped: the link would only have saved matching it
            // again next time. Anything else rolls the whole save back (#103 review).
            if (!songs.load(song.id)) {
                if (missing)
                    *missing << song.title;
                continue;
            }
            if (const SongStore::Result r = songs.updatePcoLink(song); !r.ok)
                return r.error;
        }
        return QString();
    };
}

void PlanImporter::cancelReimport()
{
    ++m_reimportRequest;   // an answer still on its way is dropped
    if (!m_reimport)
        return;
    m_reimport.reset();
    emit reimportChanged();
}
