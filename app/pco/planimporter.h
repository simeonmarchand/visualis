#pragma once

#include "pco/pcotypes.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

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

public:
    explicit PlanImporter(QObject *parent = nullptr);
    ~PlanImporter() override;

    // Wired up in main.cpp: where requests go, and the library that imports are saved to.
    void setPlanningCenter(PlanningCenter *pco);
    void setSongLibrary(SongLibrary *songs);
    // For tests: a client pointed at a fake Planning Center.
    void setClient(std::unique_ptr<PcoClient> client);

    QVariantList serviceTypes() const;
    QVariantList plans() const;
    bool busy() const { return m_pending > 0; }
    QString status() const { return m_status; }

    Q_INVOKABLE void loadServiceTypes();
    Q_INVOKABLE void loadPlans(const QString &serviceTypeId);
    // Reads the plan and its items, then saves a new service. A plan that's already in the
    // library isn't imported again (updating it is re-import's job, #56). A plan with no
    // items that could be read isn't saved, so it can be tried again.
    Q_INVOKABLE void importPlan(const QString &serviceTypeId, const QString &planId);

signals:
    void serviceTypesChanged();
    void plansChanged();
    void busyChanged();
    void statusChanged();
    // A new service is in the library.
    void imported(const QString &serviceId, const QString &title);

private:
    PcoClient *client();
    void begin();
    void end();
    void setStatus(const QString &status);
    void save(const visualis::PcoServiceType &type, const visualis::PcoPlan &plan,
              const visualis::PcoPlanItems &items);

    QPointer<PlanningCenter> m_pco;
    QPointer<SongLibrary> m_songs;
    std::unique_ptr<PcoClient> m_client;
    QList<visualis::PcoServiceType> m_types;
    QList<visualis::PcoPlan> m_plans;
    int m_plansRequest = 0;      // only the latest loadPlans() answer is shown
    int m_pending = 0;
    QString m_status;
};
