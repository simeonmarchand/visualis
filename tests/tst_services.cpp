#include "library/library.h"
#include "service/servicejson.h"
#include "service/servicestore.h"
#include "song/songstore.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "otherprogram.h"

using namespace visualis;
using Kind = ServiceItem::Kind;

namespace {

ServiceItem item(Kind kind, const QString &title)
{
    ServiceItem it;
    it.kind = kind;
    it.title = title;
    return it;
}

QStringList titles(const Service &s)
{
    QStringList out;
    for (const ServiceItem &it : s.items)
        out << it.title;
    return out;
}

} // namespace

class ServicesTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        auto r = Library::open(m_dir.filePath(QString::number(++m_n)));
        QVERIFY2(r.library, qPrintable(r.message));
        m_library = std::move(r.library);
        m_songs = std::make_unique<SongStore>(*m_library);
        m_services = std::make_unique<ServiceStore>(*m_library);

        Song song;
        song.title = "Amazing Grace";   // public domain
        song.groups = {Group{{}, "Verse 1", "#0078cc", {"Amazing grace how sweet the sound"}}};
        QVERIFY(m_songs->save(song).ok);
        m_songId = song.id;
    }

    void cleanup()
    {
        m_services.reset();
        m_songs.reset();
        m_library.reset();
    }

    void everyKindRoundTrips()
    {
        Service s;
        s.title = "Sunday Morning";
        s.date = QDate(2026, 11, 29);
        s.pcoPlanId = "12345";
        s.pcoServiceTypeId = "678";
        s.lastImportedAt = QDateTime(QDate(2026, 11, 21), QTime(9, 30), QTimeZone::UTC);

        ServiceItem song = item(Kind::Song, "Amazing Grace");
        song.ref = m_songId;
        song.customSequence = {"Verse 1", "Verse 1"};
        song.servicePosition = "during";
        song.pcoItemId = "999";
        song.overrides = QJsonObject{{"background", "blue-motion"}};
        song.importSnapshot = QJsonObject{{"title", "Amazing Grace"}, {"sequence", "V1 V1"}};
        song.pcoRemoved = true;
        ServiceItem text = item(Kind::Text, "Welcome");
        text.body = "Welcome to church!\nWe're glad you're here.";
        ServiceItem scripture = item(Kind::Scripture, "Scripture Reading: Acts 2:38");
        scripture.body = "Acts 2:38";
        s.items = {item(Kind::Header, "Worship"), song, scripture, text, item(Kind::Presentation, "Sermon"),
                   item(Kind::Media, "Countdown"), item(Kind::Placeholder, "New song TBD")};
        s.items[4].servicePosition = "during";
        s.items[5].servicePosition = "pre";

        QVERIFY(m_services->save(s).ok);
        QVERIFY(!s.id.isNull());
        QVERIFY(!s.items[0].id.isNull());
        QVERIFY(s.created.isValid());

        const std::optional<Service> loaded = m_services->load(s.id);
        QVERIFY(loaded);
        QCOMPARE(*loaded, s);
    }

    void reorderingIsSavingTheNewOrder()
    {
        Service s;
        s.title = "Order";
        s.items = {item(Kind::Header, "A"), item(Kind::Text, "B"), item(Kind::Text, "C")};
        QVERIFY(m_services->save(s).ok);
        const QList<QUuid> ids{s.items[0].id, s.items[1].id, s.items[2].id};
        s.items.move(2, 0);
        QVERIFY(m_services->save(s).ok);
        const Service loaded = *m_services->load(s.id);
        QCOMPARE(titles(loaded), (QStringList{"C", "A", "B"}));
        QCOMPARE(loaded.items[0].id, ids[2]);   // the same items, moved
    }

    void itemsTakenOutAreDeleted()
    {
        Service s;
        s.title = "Trim";
        s.items = {item(Kind::Text, "Keep"), item(Kind::Text, "Drop")};
        QVERIFY(m_services->save(s).ok);
        s.items.removeLast();
        QVERIFY(m_services->save(s).ok);
        QCOMPARE(titles(*m_services->load(s.id)), QStringList{"Keep"});
        QSqlQuery q(m_library->database());
        QVERIFY(q.exec("SELECT COUNT(*) FROM service_item") && q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    void aDeletedSongLeavesAPlaceholderNotADanglingItem()
    {
        Service s;
        s.title = "Sunday";
        ServiceItem song = item(Kind::Song, "Amazing Grace");
        song.ref = m_songId;
        s.items = {song};
        QVERIFY(m_services->save(s).ok);
        QVERIFY(m_songs->remove(m_songId));

        const Service loaded = *m_services->load(s.id);
        QCOMPARE(loaded.items[0].kind, Kind::Placeholder);
        QCOMPARE(loaded.items[0].title, QStringLiteral("Amazing Grace"));   // so the operator knows what's missing
        QVERIFY(loaded.items[0].ref.isNull());
    }

    void listsNewestFirstAndRemoves()
    {
        for (const auto &[title, date] : {std::pair{"Older", QDate(2026, 11, 22)}, std::pair{"Newer", QDate(2026, 11, 29)},
                                          std::pair{"Undated", QDate()}}) {
            Service s;
            s.title = title;
            s.date = date;
            QVERIFY(m_services->save(s).ok);
        }
        const QList<ServiceSummary> list = m_services->list();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].title, QStringLiteral("Newer"));
        QCOMPARE(list[1].title, QStringLiteral("Older"));
        QVERIFY(m_services->remove(list[0].id));
        QVERIFY(!m_services->remove(list[0].id));
        QCOMPARE(m_services->list().size(), 2);
    }

    void badServicesAreRefusedAndLeftUnchanged()
    {
        Service untitled;
        untitled.items = {item(Kind::Text, "x")};
        QVERIFY(!m_services->save(untitled).ok);
        QVERIFY(untitled.id.isNull());   // the caller's copy is untouched

        Service twice;
        twice.title = "Twice";
        ServiceItem it = item(Kind::Text, "x");
        it.id = QUuid::createUuid();
        twice.items = {it, it};
        QVERIFY(!m_services->save(twice).ok);

        Service a;
        a.title = "A";
        a.items = {item(Kind::Text, "shared")};
        QVERIFY(m_services->save(a).ok);
        Service b;
        b.title = "B";
        b.items = {a.items[0]};   // an item that belongs to service A
        QVERIFY(!m_services->save(b).ok);
    }

    void deletingAServiceDeletesItsItems()
    {
        Service s;
        s.title = "Gone";
        s.items = {item(Kind::Text, "x"), item(Kind::Header, "y")};
        QVERIFY(m_services->save(s).ok);
        QVERIFY(m_services->remove(s.id));
        QSqlQuery q(m_library->database());
        QVERIFY(q.exec("SELECT COUNT(*) FROM service_item") && q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // #50: an existing v2 library (songs only) gains services without losing anything.
    void aVersion2LibraryMigrates()
    {
        const QString folder = m_dir.filePath("v2");
        {
            auto v2 = Library::open(folder, {Library::migrations()[0], Library::migrations()[1]});
            QVERIFY2(v2.library, qPrintable(v2.message));
            QCOMPARE(v2.library->schemaVersion(), 2);
            // As v2 wrote it: today's SongStore writes columns a v2 library doesn't have yet.
            QSqlQuery q(v2.library->database());
            QVERIFY(q.exec("INSERT INTO song (id, title, created_at, updated_at) VALUES "
                           "('2d6c8a0e-0000-4000-8000-000000000001', 'Holy, Holy, Holy', "
                           "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z')"));
        }
        auto now = Library::open(folder);
        QVERIFY2(now.library, qPrintable(now.message));
        QCOMPARE(now.library->schemaVersion(), Library::latestVersion());
        QCOMPARE(SongStore(*now.library).list().size(), 1);
        Service s;
        s.title = "After the upgrade";
        QVERIFY(ServiceStore(*now.library).save(s).ok);
        QVERIFY(QFile::exists(folder + QStringLiteral("/library.sqlite.before-v%1").arg(Library::latestVersion())));
    }

    void findsTheServiceImportedFromAPlan()
    {
        Service a;
        a.title = "Imported";
        a.pcoPlanId = "70001";
        Service b;
        b.title = "By hand";
        QVERIFY(m_services->save(a).ok);
        QVERIFY(m_services->save(b).ok);
        QCOMPARE(m_services->findByPcoPlan("70001")->id, a.id);
        QVERIFY(!m_services->findByPcoPlan("70002"));
        QVERIFY(!m_services->findByPcoPlan(""));   // hand-built services have no plan
    }

    // #54: a v3 library's services gain the plan link, empty, and keep everything else.
    // #76: the duplicate check and the save are one transaction, and the write lock is
    // never waited for.
    void saveNewPlanRefusesAPlanAlreadyImported()
    {
        Service a;
        a.title = "Sunday";
        a.pcoPlanId = "70001";
        QVERIFY(m_services->saveNewPlan(a).ok);
        Service again;
        again.title = "Sunday (again)";
        again.pcoPlanId = "70001";
        const ServiceStore::Result r = m_services->saveNewPlan(again);
        QVERIFY(!r.ok && !r.busy);
        QCOMPARE(r.existing, QStringLiteral("Sunday"));
        QVERIFY(again.id.isNull());   // left as it was
        QCOMPARE(m_services->list().size(), 1);
        QVERIFY(m_services->save(again).ok);   // save() itself doesn't refuse: re-import (#56) may need it
    }

    void saveNewPlanDoesNotWaitForALockHeldElsewhere()
    {
        OtherProgram other(m_library->folder());
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        Service s;
        s.title = "Sunday";
        s.pcoPlanId = "70002";
        QElapsedTimer took;
        took.start();
        const ServiceStore::Result r = m_services->saveNewPlan(s);
        QVERIFY(!r.ok);
        QVERIFY(r.busy);
        QVERIFY2(took.elapsed() < Library::kBusyTimeoutMs / 2, qPrintable(QString("waited %1 ms").arg(took.elapsed())));
        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        QVERIFY(m_services->saveNewPlan(s).ok);
        // The connection's usual busy timeout is back for other saves.
        QSqlQuery timeout(m_library->database());
        QVERIFY(timeout.exec("PRAGMA busy_timeout") && timeout.next());
        QCOMPARE(timeout.value(0).toInt(), Library::kBusyTimeoutMs);
    }

    // Review of #103 (M6): a song's Planning Center link is saved with the service, or not at all.
    void writesInTheSameTransactionLandOrRollBackWithTheService()
    {
        Song song = *m_songs->load(m_songId);
        song.pcoSongId = "p-77";
        Service s;
        s.title = "Imported";
        s.pcoPlanId = "70077";
        s.items = {item(Kind::Song, "Amazing Grace")};
        s.items[0].ref = m_songId;
        const ServiceStore::AlsoWrite link = [&] {
            const SongStore::Result r = m_songs->updatePcoLink(song);
            return r.ok ? QString() : r.error;
        };

        const ServiceStore::AlsoWrite failing = [&] { return link().isEmpty() ? QStringLiteral("refused") : QString(); };
        Service first = s;
        QVERIFY(!m_services->saveNewPlan(first, failing).ok);
        QVERIFY(m_songs->load(m_songId)->pcoSongId.isEmpty());   // rolled back with it
        QVERIFY(m_services->list().isEmpty());

        QVERIFY(m_services->saveNewPlan(s, link).ok);
        QCOMPARE(m_songs->load(m_songId)->pcoSongId, QStringLiteral("p-77"));
        QCOMPARE(m_services->load(s.id)->items.size(), 1);

        // Busy: neither is written.
        OtherProgram other(m_library->folder());
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        song.pcoSongId = "p-78";
        const ServiceStore::Result busy = m_services->applyWithoutWaiting(s.id, [&](const Service &current) {
            Service next = current;
            next.title = "Imported again";
            return std::optional(ServiceStore::Decision{next, link});
        });
        QVERIFY(busy.busy);
        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        QCOMPARE(m_songs->load(m_songId)->pcoSongId, QStringLiteral("p-77"));
        QCOMPARE(m_services->load(s.id)->title, QStringLiteral("Imported"));
    }

    // Second review of #103 (H3, M4): load, decide and save as one step under the write lock.
    void applyWithoutWaitingDecidesOnWhatsSaved()
    {
        Service s;
        s.title = "Sunday";
        s.items = {item(Kind::Header, "Worship")};
        QVERIFY(m_services->save(s).ok);

        // A refusal writes nothing; the caller knows why.
        const ServiceStore::Result refused = m_services->applyWithoutWaiting(
            s.id, [](const Service &) -> std::optional<ServiceStore::Decision> { return std::nullopt; });
        QVERIFY(!refused.ok);
        QVERIFY(refused.error.isEmpty());

        // `decide` sees the service as saved, and what it returns is saved with `also`.
        Service saved;
        bool alsoRan = false;
        const ServiceStore::Result ok = m_services->applyWithoutWaiting(
            s.id,
            [&](const Service &current) {
                Service next = current;
                next.items[0].title = current.items[0].title + " Set";
                return std::optional(ServiceStore::Decision{next, [&] { alsoRan = true; return QString(); }});
            },
            &saved);
        QVERIFY2(ok.ok, qPrintable(ok.error));
        QVERIFY(alsoRan);
        QCOMPARE(m_services->load(s.id)->items[0].title, QStringLiteral("Worship Set"));
        QCOMPARE(saved.items[0].title, QStringLiteral("Worship Set"));

        // An error from `also` rolls it all back.
        const ServiceStore::Result rolled = m_services->applyWithoutWaiting(s.id, [](const Service &current) {
            Service next = current;
            next.title = "Renamed";
            return std::optional(ServiceStore::Decision{next, [] { return QStringLiteral("no"); }});
        });
        QVERIFY(!rolled.ok);
        QCOMPARE(m_services->load(s.id)->title, QStringLiteral("Sunday"));

        // Busy: refused at once, before `decide` runs, and nothing changes; free again, it saves.
        OtherProgram other(m_library->folder());
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        bool decided = false;
        QElapsedTimer took;
        took.start();
        const ServiceStore::Decide rename = [&](const Service &current) {
            decided = true;
            Service next = current;
            next.title = "Evening";
            return std::optional(ServiceStore::Decision{next, {}});
        };
        const ServiceStore::Result busy = m_services->applyWithoutWaiting(s.id, rename);
        QVERIFY(busy.busy);
        QVERIFY(!decided);
        QVERIFY2(took.elapsed() < Library::kBusyTimeoutMs / 2, qPrintable(QString("waited %1 ms").arg(took.elapsed())));
        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        QCOMPARE(m_services->load(s.id)->title, QStringLiteral("Sunday"));
        QVERIFY(m_services->applyWithoutWaiting(s.id, rename).ok);
        QCOMPARE(m_services->load(s.id)->title, QStringLiteral("Evening"));
    }

    void aVersion3LibraryGainsThePlanLink()
    {
        const QString folder = m_dir.filePath("v3");
        QUuid id;
        {
            auto v3 = Library::open(folder, std::vector<Migration>(Library::migrations().begin(), Library::migrations().begin() + 3));
            QVERIFY2(v3.library, qPrintable(v3.message));
            QCOMPARE(v3.library->schemaVersion(), 3);
            QSqlQuery q(v3.library->database());
            id = QUuid::createUuid();
            QVERIFY(q.exec(QStringLiteral("INSERT INTO service (id, title, pco_plan_id, created_at, updated_at) "
                                          "VALUES ('%1', 'Before', '42', '', '')").arg(id.toString(QUuid::WithoutBraces))));
        }
        auto now = Library::open(folder);
        QVERIFY2(now.library, qPrintable(now.message));
        ServiceStore store(*now.library);
        std::optional<Service> s = store.load(id);
        QVERIFY(s);
        QCOMPARE(s->pcoPlanId, QStringLiteral("42"));
        QVERIFY(s->pcoPlanUrl.isEmpty());
        s->pcoPlanUrl = "https://services.planningcenteronline.com/plans/42";
        QVERIFY(store.save(*s).ok);
        QCOMPARE(store.load(id)->pcoPlanUrl, s->pcoPlanUrl);
    }

    void aVersion5LibraryGainsTheRemovedFlag()
    {
        const QString folder = m_dir.filePath("v5");
        QUuid id;
        {
            auto v5 = Library::open(folder, std::vector<Migration>(Library::migrations().begin(), Library::migrations().begin() + 5));
            QVERIFY2(v5.library, qPrintable(v5.message));
            QSqlQuery q(v5.library->database());
            id = QUuid::createUuid();
            QVERIFY(q.exec(QStringLiteral("INSERT INTO service (id, title, created_at, updated_at) VALUES ('%1', 'Before', '', '')")
                               .arg(id.toString(QUuid::WithoutBraces))));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO service_item (id, service_id, position, kind, title) "
                                          "VALUES ('%1', '%2', 0, 'header', 'Worship')")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), id.toString(QUuid::WithoutBraces))));
        }
        auto now = Library::open(folder);
        QVERIFY2(now.library, qPrintable(now.message));
        ServiceStore store(*now.library);
        std::optional<Service> s = store.load(id);
        QVERIFY(s);
        QCOMPARE(s->items.size(), 1);
        QVERIFY(!s->items[0].pcoRemoved);
        s->items[0].pcoRemoved = true;
        QVERIFY(store.save(*s).ok);
        QVERIFY(store.load(id)->items[0].pcoRemoved);
    }

    // #56: deleting an imported item is an edit a re-import must keep.
    void deletingAnImportedItemIsRemembered()
    {
        Service s;
        s.title = "Imported";
        ServiceItem welcome = item(Kind::Placeholder, "Welcome");
        welcome.pcoItemId = "11";
        welcome.importSnapshot = QJsonObject{{"title", "Welcome"}, {"position", 1}};
        ServiceItem gone = item(Kind::Placeholder, "Offering");
        gone.pcoItemId = "12";
        gone.pcoRemoved = true;   // Planning Center took it out already
        s.items = {welcome, gone, item(Kind::Text, "Local note")};
        QVERIFY(m_services->save(s).ok);

        s.items.clear();
        QVERIFY(m_services->save(s).ok);
        const QMap<QString, QJsonObject> expected{{"11", welcome.importSnapshot}};
        QCOMPARE(s.removedPcoItems, expected);
        QCOMPARE(m_services->load(s.id)->removedPcoItems, expected);

        QVERIFY(m_services->save(s).ok);   // saving again doesn't add it twice
        QCOMPARE(m_services->load(s.id)->removedPcoItems, expected);
        s.removedPcoItems.clear();         // added back by a re-import
        QVERIFY(m_services->save(s).ok);
        QVERIFY(m_services->load(s.id)->removedPcoItems.isEmpty());
    }

    // One item on its own, as the live state journal keeps it (#93): every field comes back.
    void oneItemRoundTripsOnItsOwn()
    {
        ServiceItem it{QUuid::createUuid(), ServiceItem::Kind::Song, "Joy to the World", QUuid::createUuid(),
                       QUuid::createUuid(), {"Verse 1", "Chorus"}, "", "pre", "4412",
                       QJsonObject{{"template", QUuid::createUuid().toString(QUuid::WithoutBraces)}},
                       QJsonObject{{"title", "Joy to the World"}}};
        it.pcoRemoved = true;
        QStringList warnings;
        QCOMPARE(serviceItemFromJson(serviceItemToJson(it), &warnings), it);
        QVERIFY(warnings.isEmpty());

        QJsonObject future = serviceItemToJson(it);
        future["kind"] = "hologram";
        QCOMPARE(serviceItemFromJson(future, &warnings).kind, ServiceItem::Kind::Placeholder);
        QCOMPARE(warnings.size(), 1);
    }

    void jsonRoundTripsAndToleratesUnknownKinds()
    {
        Service s;
        s.id = QUuid::createUuid();
        s.title = "Export";
        s.date = QDate(2026, 12, 6);
        ServiceItem song = item(Kind::Song, "Amazing Grace");
        song.id = QUuid::createUuid();
        song.ref = m_songId;
        song.customSequence = {"Verse 1"};
        song.overrides = QJsonObject{{"template", "lyrics"}};
        song.pcoRemoved = true;
        s.items = {song};
        s.removedPcoItems = {{"77", QJsonObject{{"title", "Offering"}}}};
        const ServiceJsonResult r = serviceFromJson(serviceToJson(s));
        QVERIFY2(r.service, qPrintable(r.error));
        QCOMPARE(r.service->items, s.items);
        QCOMPARE(r.service->removedPcoItems, s.removedPcoItems);
        QCOMPARE(r.service->date, s.date);
        s.pcoPlanUrl = "https://services.planningcenteronline.com/plans/1";
        QCOMPARE(serviceFromJson(serviceToJson(s)).service->pcoPlanUrl, s.pcoPlanUrl);

        const ServiceJsonResult future = serviceFromJson(
            R"({"format":"visualis-service","version":1,"title":"T","items":[{"kind":"hologram","title":"Next year"}]})");
        QVERIFY(future.service);
        QCOMPARE(future.service->items[0].kind, Kind::Placeholder);
        QCOMPARE(future.warnings.size(), 1);
    }

    void badJsonIsRefused_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("garbage") << QByteArray("{nope");
        QTest::newRow("song file") << QByteArray(R"({"format":"visualis-song","version":1,"title":"x"})");
        QTest::newRow("newer") << QByteArray(R"({"format":"visualis-service","version":7,"title":"x"})");
        QTest::newRow("untitled") << QByteArray(R"({"format":"visualis-service","version":1,"title":" "})");
    }

    void badJsonIsRefused()
    {
        QFETCH(QByteArray, json);
        const ServiceJsonResult r = serviceFromJson(json);
        QVERIFY(!r.service);
        QVERIFY(!r.error.isEmpty());
    }

private:
    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<SongStore> m_songs;
    std::unique_ptr<ServiceStore> m_services;
    QUuid m_songId;
};

QTEST_GUILESS_MAIN(ServicesTest)
#include "tst_services.moc"
