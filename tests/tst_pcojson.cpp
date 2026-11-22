#include "pco/pcojson.h"

#include <QFile>
#include <QJsonDocument>
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

    // #79: every record that can't be used is named, never dropped silently.
    void badRecordsAreReported_data()
    {
        QTest::addColumn<QByteArray>("record");
        QTest::addColumn<QString>("why");
        QTest::newRow("null") << QByteArray("null") << "Record 1: not a Planning Center record";
        QTest::newRow("string") << QByteArray(R"("Item")") << "Record 1: not a Planning Center record";
        QTest::newRow("empty object") << QByteArray("{}") << "Record 1: not a Planning Center record";
        QTest::newRow("no type") << QByteArray(R"({"id":"1","attributes":{}})") << "Record 1: has no type";
        QTest::newRow("no id") << QByteArray(R"({"type":"%1","attributes":{}})") << "Record 1 (%1): has no id";
        QTest::newRow("no attributes") << QByteArray(R"({"type":"%1","id":"7"})") << "%1 7: has no attributes";
        QTest::newRow("string attributes") << QByteArray(R"({"type":"%1","id":"7","attributes":"x"})")
                                           << "%1 7: has no attributes";
    }
    void badRecordsAreReported()
    {
        QFETCH(QByteArray, record);
        QFETCH(QString, why);
        for (const QString type : {"ServiceType", "Plan", "Item"}) {
            const QByteArray body = "{\"data\":[" + QString::fromUtf8(record).arg(type).toUtf8() + "]}";
            const PcoResult<PcoDocument> doc = parsePcoDocument(body);
            QVERIFY2(doc.ok(), body.constData());
            QStringList skipped;
            qsizetype read = 0;
            if (type == "ServiceType")
                read = serviceTypesFrom(doc.value, &skipped).size();
            else if (type == "Plan")
                read = plansFrom(doc.value, "1", &skipped).size();
            else {
                const PcoPlanItems items = planItemsFrom(doc.value);
                read = items.items.size();
                skipped = items.skipped;
            }
            QCOMPARE(read, 0);
            QCOMPARE(skipped, QStringList{why.arg(type)});
        }
    }

    void aRecordOfTheWrongTypeIsReported()
    {
        QStringList skipped;
        QVERIFY(serviceTypesFrom(document("plans.json"), &skipped).isEmpty());
        QCOMPARE(skipped.size(), 2);
        QCOMPARE(skipped[0], QStringLiteral("Record 1: a Plan where a ServiceType was expected"));
    }

    void requiredFieldsAreChecked_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("why");
        QTest::newRow("service type without a name")
            << QByteArray(R"({"data":[{"type":"ServiceType","id":"1","attributes":{"name":null}}]})")
            << "ServiceType 1: has no name";
        QTest::newRow("plan without a date")
            << QByteArray(R"({"data":[{"type":"Plan","id":"5","attributes":{"dates":"Soon"}}]})")
            << "Plan 5: has no valid date";
        QTest::newRow("plan with a bad date")
            << QByteArray(R"({"data":[{"type":"Plan","id":"5","attributes":{"sort_date":"next week"}}]})")
            << "Plan 5: has no valid date";
        QTest::newRow("item without a position")
            << QByteArray(R"({"data":[{"type":"Item","id":"9","attributes":{"title":"Welcome","item_type":"item"}}]})")
            << "Item 9 (\"Welcome\"): is missing its position, kind, or title";
        QTest::newRow("item with a text position")
            << QByteArray(R"({"data":[{"type":"Item","id":"9","attributes":{"title":"Welcome","item_type":"item","sequence":"3"}}]})")
            << "Item 9 (\"Welcome\"): is missing its position, kind, or title";
        QTest::newRow("item without a kind")
            << QByteArray(R"({"data":[{"type":"Item","id":"9","attributes":{"title":"Welcome","sequence":1}}]})")
            << "Item 9 (\"Welcome\"): is missing its position, kind, or title";
        QTest::newRow("item without a title")
            << QByteArray(R"({"data":[{"type":"Item","id":"9","attributes":{"item_type":"item","sequence":1}}]})")
            << "Item 9: is missing its position, kind, or title";
    }
    void requiredFieldsAreChecked()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, why);
        const PcoDocument doc = parsePcoDocument(body).value;
        QStringList skipped;
        qsizetype read = serviceTypesFrom(doc, &skipped).size() + plansFrom(doc, "1", &skipped).size();
        const PcoPlanItems items = planItemsFrom(doc);
        read += items.items.size();
        skipped += items.skipped;
        QCOMPARE(read, 0);
        QVERIFY2(skipped.contains(why), qPrintable(skipped.join(" | ")));
    }

    // #79: a broken song, arrangement, or key keeps the item, without it, and says so.
    void brokenRelationsAreReportedAndLeftOut_data()
    {
        QTest::addColumn<QString>("relation");
        QTest::addColumn<QByteArray>("link");
        QTest::addColumn<QByteArray>("included");
        QTest::addColumn<QString>("why");
        QTest::newRow("song without an id")
            << "song" << QByteArray(R"({"type":"Song","id":"7"})")
            << QByteArray(R"({"type":"Song","attributes":{"title":"Song"}})")
            << "its song (Song 7) wasn't in Planning Center's answer";
        QTest::newRow("song not included")
            << "song" << QByteArray(R"({"type":"Song","id":"7"})") << QByteArray()
            << "its song (Song 7) wasn't in Planning Center's answer";
        QTest::newRow("song without attributes")
            << "song" << QByteArray(R"({"type":"Song","id":"7"})") << QByteArray(R"({"type":"Song","id":"7"})")
            << "its song (Song 7) has no attributes";
        QTest::newRow("song without a title")
            << "song" << QByteArray(R"({"type":"Song","id":"7"})")
            << QByteArray(R"({"type":"Song","id":"7","attributes":{"title":5}})") << "its song has no title";
        QTest::newRow("link without a type")
            << "song" << QByteArray(R"({"id":"7"})") << QByteArray() << "its song link is malformed";
        QTest::newRow("link that's a string")
            << "arrangement" << QByteArray(R"("8")") << QByteArray() << "its arrangement link is malformed";
        QTest::newRow("arrangement sequence not a list")
            << "arrangement" << QByteArray(R"({"type":"Arrangement","id":"8"})")
            << QByteArray(R"({"type":"Arrangement","id":"8","attributes":{"sequence":"Verse 1, Chorus"}})")
            << "its arrangement's section order is malformed";
        QTest::newRow("arrangement sequence with numbers")
            << "arrangement" << QByteArray(R"({"type":"Arrangement","id":"8"})")
            << QByteArray(R"({"type":"Arrangement","id":"8","attributes":{"sequence":["Verse 1", 2]}})")
            << "its arrangement's section order is malformed";
        QTest::newRow("key without attributes")
            << "key" << QByteArray(R"({"type":"Key","id":"6"})") << QByteArray(R"({"type":"Key","id":"6","attributes":null})")
            << "its key (Key 6) has no attributes";
    }
    void brokenRelationsAreReportedAndLeftOut()
    {
        QFETCH(QString, relation);
        QFETCH(QByteArray, link);
        QFETCH(QByteArray, included);
        QFETCH(QString, why);
        // Built as JSON values, not spliced strings (moc misreads raw strings with unbalanced braces).
        QJsonObject item = QJsonDocument::fromJson(
            R"({"type":"Item","id":"9","attributes":{"title":"Song One","item_type":"song","sequence":1}})").object();
        item["relationships"] = QJsonObject{
            {relation, QJsonObject{{"data", QJsonDocument::fromJson("[" + link + "]").array().at(0)}}}};
        QJsonArray inc;
        if (!included.isEmpty())
            inc.append(QJsonDocument::fromJson(included).object());
        const QByteArray body = QJsonDocument(QJsonObject{{"data", QJsonArray{item}}, {"included", inc}}).toJson();
        const PcoResult<PcoDocument> doc = parsePcoDocument(body);
        QVERIFY2(doc.ok(), body.constData());
        const PcoPlanItems plan = planItemsFrom(doc.value);
        QCOMPARE(plan.items.size(), 1);   // the item itself is fine
        QVERIFY(!plan.items[0].song && !plan.items[0].arrangement && !plan.items[0].key);
        QCOMPARE(plan.skipped, QStringList{"Item 9 (\"Song One\"): " + why});
    }

    void aNullArrangementSequenceIsJustEmpty()
    {
        const QByteArray body = R"({"data":[{"type":"Item","id":"9","attributes":{"title":"Song","item_type":"song","sequence":1},
            "relationships":{"arrangement":{"data":{"type":"Arrangement","id":"8"}}}}],
            "included":[{"type":"Arrangement","id":"8","attributes":{"name":"Default","sequence":null}}]})";
        const PcoPlanItems plan = planItemsFrom(parsePcoDocument(body).value);
        QVERIFY(plan.skipped.isEmpty());
        QVERIFY(plan.items[0].arrangement);
        QVERIFY(plan.items[0].arrangement->sequence.isEmpty());
    }

    void theFixturesHaveNothingToReport()
    {
        QStringList skipped;
        serviceTypesFrom(document("service_types.json"), &skipped);
        plansFrom(document("plans.json"), "1001", &skipped);
        plansFrom(document("plan.json"), "1001", &skipped);
        QVERIFY2(skipped.isEmpty(), qPrintable(skipped.join(" | ")));
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
        QTest::newRow("next is a number") << QByteArray(R"({"data":[],"links":{"next":3}})");
        QTest::newRow("next is an object") << QByteArray(R"({"data":[],"links":{"next":{"href":"x"}}})");
        QTest::newRow("links is a string") << QByteArray(R"({"data":[],"links":"next"})");
        QTest::newRow("included is an object") << QByteArray(R"({"data":[],"included":{"type":"Song"}})");
        QTest::newRow("included holds a string") << QByteArray(R"({"data":[],"included":["Song"]})");
    }
    void malformedDocuments()
    {
        QFETCH(QByteArray, body);
        const PcoResult<PcoDocument> r = parsePcoDocument(body);
        QCOMPARE(r.error.kind, PcoError::Malformed);
        QVERIFY(!r.error.message.isEmpty());
    }

    void absentOrNullLinksAndIncludedAreFine()
    {
        for (const QByteArray body : {QByteArray(R"({"data":[]})"),
                                      QByteArray(R"({"data":[],"links":{"next":null},"included":null})"),
                                      QByteArray(R"({"data":[],"links":null})")}) {
            const PcoResult<PcoDocument> r = parsePcoDocument(body);
            QVERIFY2(r.ok(), body.constData());
            QVERIFY(r.value.next.isEmpty());
        }
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
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "7"}}), std::optional<qint64>(7000));
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "0"}}), std::optional<qint64>(0));
        // No Retry-After: the reported period.
        QCOMPARE(pcoRetryDelayMs({{"X-PCO-API-Request-Rate-Period", "20"}}), std::optional<qint64>(20000));
        // Nothing to go on: don't guess.
        QCOMPARE(pcoRetryDelayMs({}), std::nullopt);
        // Present but unreadable: too long to wait for, not absent (#79).
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "Wed, 21 Oct 2026 07:28:00 GMT"}}), std::optional(kPcoWaitTooLongMs));
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "-5"}}), std::optional(kPcoWaitTooLongMs));
    }

    void hugeHeaderValuesDontOverflow()
    {
        constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;
        // Over INT_MAX seconds, and over INT_MAX milliseconds once multiplied.
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "99999999999"}}), std::optional<qint64>(kDayMs));
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "3000000"}}), std::optional<qint64>(kDayMs));
        QCOMPARE(pcoRetryDelayMs({{"X-PCO-API-Request-Rate-Period", "3000000"}}), std::optional<qint64>(kDayMs));
        QCOMPARE(pcoPacingDelayMs({100, 100, 3'000'000}), kDayMs);
        QCOMPARE(pcoPacingDelayMs({100, 100, 3600}), 3'600'000LL);
    }

    // #79: numbers too big for an int saturate instead of wrapping to 0 (which disabled pacing).
    void rateLimitNumbersParseWide()
    {
        const PcoRateLimit huge = pcoRateLimitFrom({{"X-PCO-API-Request-Rate-Count", "99999999999999999999999"},
                                                    {"X-PCO-API-Request-Rate-Limit", "3000000000"},
                                                    {"X-PCO-API-Request-Rate-Period", "5000000000"}});
        QCOMPARE(huge.count, std::numeric_limits<qint64>::max());
        QCOMPARE(huge.limit, 3'000'000'000LL);
        QCOMPARE(huge.periodSeconds, 5'000'000'000LL);
        QCOMPARE(pcoPacingDelayMs(huge), 24LL * 60 * 60 * 1000);   // used up; the wait clamps to a day
        const PcoRateLimit junk = pcoRateLimitFrom({{"X-PCO-API-Request-Rate-Count", "-3"},
                                                    {"X-PCO-API-Request-Rate-Limit", "lots"}});
        QCOMPARE(junk.count, 0);
        QCOMPARE(junk.limit, 0);
        QCOMPARE(pcoRetryDelayMs({{"Retry-After", "99999999999999999999999"}}), std::optional<qint64>(24LL * 60 * 60 * 1000));
    }
};

QTEST_GUILESS_MAIN(PcoJsonTest)
#include "tst_pcojson.moc"
