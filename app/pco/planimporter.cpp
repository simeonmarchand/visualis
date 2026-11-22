#include "pco/planimporter.h"

#include "library/songlibrary.h"
#include "pco/pcoclient.h"
#include "pco/planimport.h"
#include "pco/planningcenter.h"
#include "pco/songlink.h"
#include "service/servicestore.h"
#include "song/songstore.h"

#include <QDate>
#include <QDateTime>

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
    m_songs = songs;
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
        setStatus(tr("This plan is already in the library as \"%1\". Updating it from Planning Center isn't "
                     "supported yet.").arg(existing->title));
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
    pco->plan(serviceTypeId, planId, [self, pco, type, serviceTypeId, planId](PcoResult<PcoPlan> plan) {
        if (!self)
            return;
        if (!plan.ok()) {
            self->end();
            self->setStatus(plan.error.message);
            return;
        }
        pco->planItems(serviceTypeId, planId, [self, type, plan = plan.value](PcoResult<PcoPlanItems> items) {
            if (!self)
                return;
            self->end();
            if (!items.ok()) {
                self->setStatus(items.error.message);
                return;
            }
            self->save(type, plan, items.value);
        });
    });
}

void PlanImporter::save(const PcoServiceType &type, const PcoPlan &plan, const PcoPlanItems &items)
{
    if (!m_songs || !m_songs->library()) {
        setStatus(tr("The library isn't open, so there's nowhere to save the service."));
        return;
    }
    ServiceStore store(*m_songs->library());
    // Checked again: the same plan may have been imported while this one was reading.
    if (const auto existing = store.findByPcoPlan(plan.id)) {
        setStatus(tr("This plan is already in the library as \"%1\".").arg(existing->title));
        return;
    }
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
    SongStore songStore(*m_songs->library());
    const SongLinkReport links = linkSongs(r.service, songStore);
    const ServiceStore::Result saved = store.save(r.service);
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
