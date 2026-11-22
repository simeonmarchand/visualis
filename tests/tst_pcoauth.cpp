#include "pco/pcoauth.h"

#include <QtTest>

using namespace visualis;

class PcoAuthTest : public QObject
{
    Q_OBJECT

private slots:
    void aPersonalAccessTokenIsHttpBasic()
    {
        const PersonalAccessToken token({"  app123 ", " s3cret\n"});
        QCOMPARE(token.authorization(), QByteArray("Basic ") + QByteArray("app123:s3cret").toBase64());
    }

    void credentialsNeedBothParts()
    {
        QVERIFY(PcoCredentials({"a", "b"}).isValid());
        QVERIFY(!PcoCredentials({"a", " "}).isValid());
        QVERIFY(!PcoCredentials({"", "b"}).isValid());
    }

    void theOrganizationComesFromTheApiRoot()
    {
        const OrganizationResult r = organizationFromResponse(
            200, R"({"data":{"type":"Organization","id":"1","attributes":{"name":"Grace Church","ccli":"123"}}})");
        QCOMPARE(r.name, QStringLiteral("Grace Church"));
        QVERIFY(r.error.isEmpty());
    }

    void failuresSayWhatWentWrong_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("mentions");
        QTest::newRow("rejected") << 401 << QByteArray("{}") << "didn't accept";
        QTest::newRow("no Services") << 403 << QByteArray("{}") << "permissions";
        QTest::newRow("rate limited") << 429 << QByteArray("{}") << "busy";
        QTest::newRow("unreachable") << 0 << QByteArray() << "internet";
        QTest::newRow("server error") << 503 << QByteArray("oops") << "503";
        QTest::newRow("not json") << 200 << QByteArray("<html>") << "expected";
        QTest::newRow("not an organization") << 200 << QByteArray(R"({"data":{"type":"Person","attributes":{"name":"x"}}})") << "expected";
    }

    void failuresSayWhatWentWrong()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, mentions);
        const OrganizationResult r = organizationFromResponse(status, body);
        QVERIFY(r.name.isEmpty());
        QVERIFY2(r.error.contains(mentions), qPrintable(r.error));
    }
};

QTEST_GUILESS_MAIN(PcoAuthTest)
#include "tst_pcoauth.moc"
