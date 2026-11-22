#include "library/library.h"
#include "service/servicejson.h"
#include "service/servicestore.h"
#include "song/songstore.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

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
            SongStore songs(*v2.library);
            Song old;
            old.title = "Holy, Holy, Holy";
            QVERIFY(songs.save(old).ok);
        }
        auto now = Library::open(folder);
        QVERIFY2(now.library, qPrintable(now.message));
        QCOMPARE(now.library->schemaVersion(), 3);
        QCOMPARE(SongStore(*now.library).list().size(), 1);
        Service s;
        s.title = "After the upgrade";
        QVERIFY(ServiceStore(*now.library).save(s).ok);
        QVERIFY(QFile::exists(folder + "/library.sqlite.before-v3"));
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
        s.items = {song};
        const ServiceJsonResult r = serviceFromJson(serviceToJson(s));
        QVERIFY2(r.service, qPrintable(r.error));
        QCOMPARE(r.service->items, s.items);
        QCOMPARE(r.service->date, s.date);

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
