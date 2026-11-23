#pragma once

#include "pco/pcotypes.h"
#include "pco/planimport.h"
#include "pco/reimportproposal.h"
#include "pco/songlink.h"
#include "song/song.h"
#include "service/service.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
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
    // "keptPosition", "relink", "changedButDeleted", "unreadable" or "linked" (a song
    // placeholder applying links to the library). An "added" line with a `pcoItemId` is an
    // item the operator chose to add back.
    Q_PROPERTY(QString reimportServiceId READ reimportServiceId NOTIFY reimportChanged)
    Q_PROPERTY(QVariantList reimportChanges READ reimportChanges NOTIFY reimportChanged)
    Q_PROPERTY(QString reimportSummary READ reimportSummary NOTIFY reimportChanged)
    // Whether Apply would change anything, shown or not (a refreshed record of the plan).
    Q_PROPERTY(bool reimportNeedsSave READ reimportNeedsSave NOTIFY reimportChanged)
    // What couldn't be read from Planning Center, for the operator.
    Q_PROPERTY(QStringList reimportNotes READ reimportNotes NOTIFY reimportChanged)
    // Set when the service changed while the diff was open, so this is a new one to look at.
    Q_PROPERTY(QString reimportNotice READ reimportNotice NOTIFY reimportChanged)

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
    bool reimportNeedsSave() const;
    QStringList reimportNotes() const { return m_reimport ? m_reimport->fresh.notes : QStringList(); }
    QString reimportNotice() const { return m_reimport ? m_reimport->notice : QString(); }

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
    // Deleted items to add back (Planning Center ids from "changedButDeleted" lines): the diff
    // is worked out again, so what's shown is what Apply does.
    Q_INVOKABLE void setReimportAddBack(const QStringList &pcoItemIds);
    // Saves exactly the diff shown (#103 review). Under the library's write lock it works the
    // diff out again from the service as saved; if that's not what was shown, nothing is
    // saved and the new diff is offered instead (reimportNotice). Like an import's save it
    // never waits on another program's lock (#76): it retries, then gives up with the service
    // unchanged. Returns whether a save was started; `reimported` says when it's done.
    Q_INVOKABLE bool applyReimport();
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
    // Reads a plan, then its items, and hands both to `read`; a failure is the status. With
    // `stillWanted`, an answer it turns down is dropped. Calls end() once the reads are done.
    void readPlan(PcoClient *pco, const QString &typeId, const QString &planId, const std::function<bool()> &stillWanted,
                  const std::function<void(const visualis::PcoPlan &, const visualis::PcoPlanItems &)> &read);
    void save(const visualis::PcoServiceType &type, const visualis::PcoPlan &plan,
              const visualis::PcoPlanItems &items, int library);
    // One attempt at saving; false if another program had the library locked. `library` is
    // m_libraryChanges when importPlan() was called: a library opened since isn't the one the
    // operator imported into.
    bool trySave(const visualis::PlanImport &import, const visualis::SongLinkReport &links, int library);

    // Calls `attempt` until it says it's done (true), every kLockRetryMs while it says the
    // library is busy (false), and `gaveUp` once `deadline` passes: never waiting on the lock.
    // What to add to the status about songs: groups a song lacks, links that couldn't be saved.
    static QString linkNotes(const visualis::SongLinkReport &links, const QStringList &missing);
    void whileBusy(QDeadlineTimer deadline, const std::function<bool()> &attempt, const std::function<void()> &gaveUp);
    void offerReimport(const QUuid &serviceId, const visualis::PlanImport &fresh, int library);
    // One attempt at applying; false if another program had the library locked.
    bool tryApply(int request);
    // The library songs linkSongs() updated, to write in the service's transaction. A song
    // that's gone from the library is skipped and named in `missing`; any other failure is
    // returned, so the whole save rolls back. `missing` must outlive the save: ServiceStore
    // runs the writes before it returns.
    std::function<QString()> linkWrites(const QList<visualis::Song> &linked, QStringList *missing);

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
        visualis::PlanImport fresh;   // the plan as read, as serviceFromPlan made it
        int library = 0;              // m_libraryChanges when read: only applied to that library
        QSet<QString> addBack;
        visualis::ReimportProposal shown;   // what the operator is looking at, and approves with Apply
        QString notice;
    };
    std::optional<PendingReimport> m_reimport;
    // What applying `pending` would do now, from the service as saved; nothing if it's gone.
    std::optional<visualis::ReimportProposal> propose(const PendingReimport &pending);
    // Works out what `pending` would do and offers it for Apply or Cancel.
    void show(PendingReimport pending);
    int m_reimportRequest = 0;   // only the latest reimport() answer is offered
};
