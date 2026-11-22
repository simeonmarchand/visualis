#pragma once

#include "pco/pcotypes.h"
#include "pco/planimport.h"
#include "pco/songlink.h"
#include "service/service.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

class PcoClient;
class PlanningCenter;
class SongLibrary;

// The plan picker's model (#54): service types, then a type's plans (upcoming first), then
// importing one plan into a new service in the library. Every read is asynchronous, so a
// slow or dead network never holds up the operator window or the outputs. The imported
// service is saved whole, so it opens and runs with the network off.
class PlanImporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // {id, name} per service type.
    Q_PROPERTY(QVariantList serviceTypes READ serviceTypes NOTIFY serviceTypesChanged)
    // {id, label, dates, title, itemsCount, url} per plan of the chosen type.
    Q_PROPERTY(QVariantList plans READ plans NOTIFY plansChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // What the last action did, for the operator.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // A re-import read and waiting for Apply or Cancel (#56): the service it's for (empty when
    // none), its diff as {kind, title, field, detail, pcoItemId}, and the diff in a sentence.
    // `kind` is "added", "removed", "restored", "moved", "changed", "keptEdit",
    // "keptPosition", "relink" or "changedButDeleted".
    Q_PROPERTY(QString reimportServiceId READ reimportServiceId NOTIFY reimportChanged)
    Q_PROPERTY(QVariantList reimportChanges READ reimportChanges NOTIFY reimportChanged)
    Q_PROPERTY(QString reimportSummary READ reimportSummary NOTIFY reimportChanged)

public:
    explicit PlanImporter(QObject *parent = nullptr);
    ~PlanImporter() override;

    // Wired up in main.cpp: where requests go, and the library that imports are saved to.
    void setPlanningCenter(PlanningCenter *pco);
    void setSongLibrary(SongLibrary *songs);
    // For tests: a client pointed at a fake Planning Center.
    void setClient(std::unique_ptr<PcoClient> client);
    // While another program holds the library's write lock, a save is retried this often,
    // for this long, before the import gives up (#76). Settable for tests.
    static constexpr int kLockRetryMs = 250;
    void setLockWaitLimit(int ms) { m_lockWaitLimitMs = ms; }

    QVariantList serviceTypes() const;
    QVariantList plans() const;
    bool busy() const { return m_pending > 0; }
    QString status() const { return m_status; }
    QString reimportServiceId() const;
    QVariantList reimportChanges() const;
    QString reimportSummary() const;

    Q_INVOKABLE void loadServiceTypes();
    Q_INVOKABLE void loadPlans(const QString &serviceTypeId);
    // Reads the plan and its items, then saves a new service. A plan that's already in the
    // library is re-imported into its service instead. A plan with no items that could be
    // read isn't saved, so it can be tried again.
    Q_INVOKABLE void importPlan(const QString &serviceTypeId, const QString &planId);

    // Re-import (#56): reads the service's plan again and merges it with the operator's
    // copy, without saving: the diff waits for applyReimport() or cancelReimport(). Nothing
    // the operator did is lost (mergeReimport).
    Q_INVOKABLE void reimport(const QString &serviceId);
    // Saves the merge, adding back the deleted items in `addBack` (Planning Center item ids
    // from "changedButDeleted" lines). Songs the library is sure of are linked, as on import.
    Q_INVOKABLE bool applyReimport(const QStringList &addBack = {});
    Q_INVOKABLE void cancelReimport();

signals:
    void serviceTypesChanged();
    void plansChanged();
    void busyChanged();
    void statusChanged();
    // A new service is in the library.
    void imported(const QString &serviceId, const QString &title);
    void reimportChanged();
    // A re-import was saved into this service.
    void reimported(const QString &serviceId);

private:
    PcoClient *client();
    void begin();
    void end();
    void setStatus(const QString &status);
    void save(const visualis::PcoServiceType &type, const visualis::PcoPlan &plan,
              const visualis::PcoPlanItems &items, int library);
    // One attempt at saving. `library` is m_libraryChanges when importPlan() was called: a
    // library opened since isn't the one the operator imported into.
    void trySave(visualis::PlanImport import, visualis::SongLinkReport links, QDeadlineTimer deadline,
                 int library);
    void gaveUpWaiting(const QString &title);

    void offerReimport(const QUuid &serviceId, const visualis::Service &fresh);

    QPointer<PlanningCenter> m_pco;
    QPointer<SongLibrary> m_songs;
    std::unique_ptr<PcoClient> m_client;
    QList<visualis::PcoServiceType> m_types;
    QList<visualis::PcoPlan> m_plans;
    int m_plansRequest = 0;      // only the latest loadPlans() answer is shown
    int m_pending = 0;
    QString m_status;
    int m_lockWaitLimitMs = 10000;
    int m_libraryChanges = 0;
    struct PendingReimport {
        QUuid serviceId;
        visualis::Service fresh;   // the plan as read, as serviceFromPlan made it
        QVariantList changes;
        QString summary;
    };
    std::optional<PendingReimport> m_reimport;
    int m_reimportRequest = 0;   // only the latest reimport() answer is offered
};
