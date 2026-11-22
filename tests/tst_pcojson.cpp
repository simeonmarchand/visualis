#include "pco/pcojson.h"

#include <QFile>
#include <QtTest>

using namespace visualis;

// #53: reading Planning Center's answers, from synthetic fixtures shaped like the P3
// sandbox dump (tests/fixtures/pco). No network.
class PcoJsonTest : public QObject
{
    Q_OBJECT

    static QByteArray fixture(const QString &name)
    {
        QFile f(QStringLiteral(FIXTURES_DIR "/pco/") + name);
        if (!f.open(QIODevice::ReadOnly))
            qFatal("missing fixture %s", qPrintable(name));
        return f.readAll().replace("{{BASE}}", "https://api.planningcenteronline.com/services/v2/");
    }

    static PcoDocument document(const QString &name)
    {
        const PcoResult<PcoDocument> r = parsePcoDocument(fixture(name));
        if (!r.ok())
            qFatal("fixture %s didn't parse", qPrintable(name));
        return r.value;
    }

    // Two pages merged, as the client does.
    static PcoDocument allItems()
    {
        PcoDocument doc = document("items_page1.json");
        const PcoDocument second = document("items_page2.json");
        for (const QJsonValue &v : second.data)
            doc.data.append(v);
        for (const QJsonValue &v : second.included)
            doc.included.append(v);
        return doc;
    }

private slots:
    void serviceTypes()
    {
        const QList<PcoServiceType> types = serviceTypesFrom(document("service_types.json"));
        QCOMPARE(types.size(), 2);
        QCOMPARE(types[0].id, QStringLiteral("1001"));
        QCOMPARE(types[0].name, QStringLiteral("Sunday Morning"));
        QCOMPARE(types[1].name, QStringLiteral("Youth Night"));
    }

    void plans()
    {
        const QList<PcoPlan> plans = plansFrom(document("plans.json"), "1001");
        QCOMPARE(plans.size(), 2);
        const PcoPlan &p = plans[0];
        QCOMPARE(p.id, QStringLiteral("5001"));
        QCOMPARE(p.serviceTypeId, QStringLiteral("1001"));
        QVERIFY(p.title.isEmpty());   // null in the API
        QCOMPARE(p.dates, QStringLiteral("January 10, 2027"));
        QCOMPARE(p.sortDate, QDateTime(QDate(2027, 1, 10), QTime(10, 0), QTimeZone::UTC));
        QCOMPARE(p.itemsCount, 5);
        QCOMPARE(p.planningCenterUrl, QUrl("https://services.planningcenteronline.com/plans/5001"));
        QCOMPARE(plans[1].title, QStringLiteral("Baptism Sunday"));
        QCOMPARE(plans[1].seriesTitle, QStringLiteral("Acts"));
    }

    void aSinglePlanDocument()
    {
        const PcoDocument doc = document("plan.json");
        QCOMPARE(doc.data.size(), 1);
        const QList<PcoPlan> plans = plansFrom(doc, "1001");
        QCOMPARE(plans.size(), 1);
        QVERIFY(plans[0].planningCenterUrl.isValid());
    }

    void theNextLinkIsRead()
    {
        QCOMPARE(document("items_page1.json").next,
                 QUrl("https://api.planningcenteronline.com/services/v2/service_types/1001/plans/5001/items"
                      "?include=song,arrangement,key&offset=3&per_page=3"));
        QVERIFY(document("items_page2.json").next.isEmpty());
    }

    void itemsAreInPlanOrderAcrossPages()
    {
        const PcoPlanItems plan = planItemsFrom(allItems());
        QVERIFY(plan.skipped.isEmpty());
        QStringList ids;
        for (const PcoItem &i : plan.items)
            ids << i.id;
        // Page two lists 9005 before 9004; `sequence` decides.
        QCOMPARE(ids, (QStringList{"9001", "9002", "9003", "9004", "9005"}));
    }

    void itemKindsAndPositions()
    {
        const QList<PcoItem> items = planItemsFrom(allItems()).items;
        QCOMPARE(items[0].type, PcoItemType::Media);
        QCOMPARE(items[0].servicePosition, PcoServicePosition::Pre);
        QCOMPARE(items[0].lengthSeconds, 300);
        QCOMPARE(items[1].type, PcoItemType::Header);
        QCOMPARE(items[2].type, PcoItemType::Song);
        QCOMPARE(items[3].type, PcoItemType::Item);
        QCOMPARE(items[3].title, QStringLiteral("Acts 2:38"));   // scripture is a plain item (P3 finding 6)
        QVERIFY(!items[3].song);
        QVERIFY(!items[3].arrangement);
        QCOMPARE(items[2].description, QStringLiteral("Start soft"));
        QCOMPARE(items[2].htmlDetails, QStringLiteral("<p>Start soft</p>"));
    }

    void aSongItemResolvesItsSongArrangementAndKey()
    {
        const PcoItem item = planItemsFrom(allItems()).items[2];
        QVERIFY(item.song);
        QCOMPARE(item.song->id, QStringLiteral("7001"));
        QCOMPARE(item.song->ccliNumber, QStringLiteral("1234567"));   // a number in the JSON
        QCOMPARE(item.song->author, QStringLiteral("A. Writer and B. Writer"));
        QCOMPARE(item.song->copyright, QStringLiteral("2020 Example Music Publishing"));   // trimmed
        QVERIFY(item.arrangement);
        QCOMPARE(item.arrangement->name, QStringLiteral("Default"));
        QCOMPARE(item.arrangement->bpm, 130);
        QCOMPARE(item.arrangement->meter, QStringLiteral("4/4"));
        QCOMPARE(item.arrangement->lengthSeconds, 454);
        QCOMPARE(item.arrangement->sequence,
                 (QStringList{"Intro", "Verse 1", "Pre Chorus 1", "Chorus", "Chorus", "Ending"}));
        QVERIFY(item.key);
        QCOMPARE(item.keyLabel(), QStringLiteral("Db"));
        QVERIFY(item.customArrangementSequence.isEmpty());
        QCOMPARE(item.sequence(), item.arrangement->sequence);
    }

    void theServicesOwnOrderWins()
    {
        const PcoItem item = planItemsFrom(allItems()).items[4];
        QCOMPARE(item.sequence(), (QStringList{"Verse 1", "Chorus", "Bridge", "Chorus"}));
        QCOMPARE(item.arrangement->sequence.size(), 6);
        QVERIFY(item.song->ccliNumber.isEmpty());   // null
        QCOMPARE(item.arrangement->bpm, 0);
        QCOMPARE(item.keyLabel(), QStringLiteral("Capo 2"));   // no key_name on the item: the key's name
    }

    void aMissingIncludedRecordLeavesTheRelationEmpty()
    {
        PcoDocument doc = document("items_page1.json");
        doc.included = {};
        const PcoItem item = planItemsFrom(doc).items[2];
        QVERIFY(!item.song);
        QVERIFY(!item.arrangement);
        QCOMPARE(item.keyLabel(), QStringLiteral("Db"));   // from the item's key_name
    }

    void anItemWithoutAnIdIsSkippedAndReported()
    {
        const QByteArray body = R"({"data":[
            {"type":"Item","attributes":{"title":"Broken","sequence":1}},
            {"type":"Item","id":"1","attributes":{"title":"Fine","item_type":"surprise","sequence":2}}]})";
        const PcoPlanItems plan = planItemsFrom(parsePcoDocument(body).value);
        QCOMPARE(plan.items.size(), 1);
        QCOMPARE(plan.items[0].type, PcoItemType::Item);   // an unknown kind reads as a plain item
        QCOMPARE(plan.skipped.size(), 1);
        QVERIFY(plan.skipped[0].contains("Broken"));
    }

    void malformedDocuments_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("html") << QByteArray("<html>Bad gateway</html>");
        QTest::newRow("truncated") << fixture("items_page1.json").left(200);
        QTest::newRow("array root") << QByteArray("[1,2]");
        QTest::newRow("no data") << QByteArray(R"({"errors":[]})");
        QTest::newRow("data is a string") << QByteArray(R"({"data":"x"})");
    }
    void malformedDocuments()
    {
        QFETCH(QByteArray, body);
        const PcoResult<PcoDocument> r = parsePcoDocument(body);
        QCOMPARE(r.error.kind, PcoError::Malformed);
        QVERIFY(!r.error.message.isEmpty());
    }

    void wrongRecordTypesAreIgnored()
    {
        QVERIFY(serviceTypesFrom(document("plans.json")).isEmpty());
        QVERIFY(plansFrom(document("service_types.json"), "1").isEmpty());
    }

    void statusErrors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("kind");
        QTest::newRow("ok") << 200 << int(PcoError::None);
        QTest::newRow("offline") << 0 << int(PcoError::Offline);
        QTest::newRow("401") << 401 << int(PcoError::Unauthorized);
        QTest::newRow("403") << 403 << int(PcoError::Forbidden);
        QTest::newRow("404") << 404 << int(PcoError::NotFound);
        QTest::newRow("429") << 429 << int(PcoError::RateLimited);
        QTest::newRow("500") << 500 << int(PcoError::Server);
        QTest::newRow("503") << 503 << int(PcoError::Server);
        QTest::newRow("418") << 418 << int(PcoError::Other);
    }
    void statusErrors()
    {
        QFETCH(int, status);
        QFETCH(int, kind);
        const PcoError e = pcoErrorForStatus(status);
        QCOMPARE(int(e.kind), kind);
        QCOMPARE(e.message.isEmpty(), kind == PcoError::None);
    }

    void rateLimitHeaders()
    {
        const PcoHeaders h{{"x-pco-api-request-rate-count", "12"},
                           {"X-PCO-API-Request-Rate-Limit", "100"},
                           {"X-PCO-API-Request-Rate-Period", "20"}};
        const PcoRateLimit r = pcoRateLimitFrom(h);
        QCOMPARE(r.count, 12);
        QCOMPARE(r.limit, 100);
        QCOMPARE(r.periodSeconds, 20);
        QCOMPARE(pcoPacingDelayMs(r), 0);
        QCOMPARE(pcoPacingDelayMs({100, 100, 20}), 20000);   // used up: wait out the period
        QCOMPARE(pcoPacingDelayMs({}), 0);                    // no headers: no pacing
    }

    void retryDelay()
    {
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "7"}}), std::optional<int>(7000));
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "0"}}), std::optional<int>(0));
        // No Retry-After: the reported period.
        QCOMPARE(pcoRetryDelayMs({{"X-PCO-API-Request-Rate-Period", "20"}}), std::optional<int>(20000));
        // Nothing to go on, or a date instead of seconds: don't guess.
        QCOMPARE(pcoRetryDelayMs({}), std::nullopt);
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "Wed, 21 Oct 2026 07:28:00 GMT"}}), std::nullopt);
    }
};

QTEST_GUILESS_MAIN(PcoJsonTest)
#include "tst_pcojson.moc"
