#include "pco/credentialstore.h"
#include "pco/planningcenter.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace visualis;

namespace {

// A stand-in for Planning Center: answers each request with the given status and body, and
// remembers the Authorization header it was sent.
class FakePlanningCenter : public QObject
{
public:
    int status = 200;
    QByteArray body = R"({"data":{"type":"Organization","id":"1","attributes":{"name":"Test Church"}}})";
    QByteArray lastAuthorization;
    int requests = 0;

    FakePlanningCenter()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = m_server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                m_buffer += s->readAll();
                if (!m_buffer.contains("\r\n\r\n"))
                    return;
                for (const QByteArray &line : m_buffer.split('\n')) {
                    if (line.toLower().startsWith("authorization:"))
                        lastAuthorization = line.mid(line.indexOf(':') + 1).trimmed();
                }
                m_buffer.clear();
                ++requests;
                s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\n"
                         "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/services/v2/").arg(m_server.serverPort())); }

private:
    QTcpServer m_server;
    QByteArray m_buffer;
};

// The real store, but saving always fails: as if the Keychain refused the new token.
class FailingSaves : public CredentialStore
{
public:
    using CredentialStore::CredentialStore;
    Result save(const PcoCredentials &) const override { return {false, "Couldn't save to the Keychain: test"}; }
};

// The real store, noting the thread of every call.
class RecordingStore : public CredentialStore
{
public:
    using CredentialStore::CredentialStore;
    Result save(const PcoCredentials &c) const override { note(); return CredentialStore::save(c); }
    std::optional<PcoCredentials> load(QString *error) const override { note(); return CredentialStore::load(error); }
    Result remove() const override { note(); return CredentialStore::remove(); }

    QList<QThread *> threads() const
    {
        QMutexLocker lock(&m_mutex);
        return m_threads;
    }

private:
    void note() const
    {
        QMutexLocker lock(&m_mutex);
        m_threads << QThread::currentThread();
    }
    mutable QMutex m_mutex;
    mutable QList<QThread *> m_threads;
};

} // namespace

// #52: the token is checked before it's kept, kept only in the Keychain, and never written
// anywhere else. Uses its own Keychain entry and settings file, never the real ones: every
// PlanningCenter here is given its store, so none opens the production entry (#73).
class PlanningCenterTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDir.isValid());
        QCoreApplication::setOrganizationName("VisualisTests");
        QCoreApplication::setApplicationName("PlanningCenterTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        m_service = "org.visualis.tests." + QUuid::createUuid().toString(QUuid::WithoutBraces);

        if (!CredentialStore::isSupported())
            QSKIP("no credential store on this system yet");
        // A CI machine may have no unlocked Keychain; say so rather than fail.
        const CredentialStore probe(m_service);
        if (const auto r = probe.save({"probe", "probe"}); !r.ok)
            QSKIP(qPrintable("Keychain not usable here: " + r.error));
        probe.remove();
    }

    void cleanup()
    {
        CredentialStore(m_service).remove();
        QSettings().clear();
    }

    void theKeychainKeepsAndForgetsCredentials()
    {
        const CredentialStore store(m_service);
        QVERIFY(!store.load());
        QVERIFY(store.save({"app", "one"}).ok);
        QVERIFY(store.save({"app", "two"}).ok);   // replaces
        QCOMPARE(store.load()->secret, QStringLiteral("two"));
        QVERIFY(store.remove().ok);
        QVERIFY(!store.load());
        QVERIFY(store.remove().ok);   // nothing there: still fine
    }

    void anAcceptedTokenIsSavedAndShowsItsOrganization()
    {
        FakePlanningCenter server;
        PlanningCenter pco(store());
        pco.setBaseUrl(server.url());
        QSignalSpy finished(&pco, &PlanningCenter::signInFinished);

        pco.signIn(" my-app-id ", "my-secret");
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QVERIFY(pco.signedIn());
        QCOMPARE(pco.organization(), QStringLiteral("Test Church"));
        QCOMPARE(server.lastAuthorization, QByteArray("Basic ") + QByteArray("my-app-id:my-secret").toBase64());
        QCOMPARE(CredentialStore(m_service).load()->appId, QStringLiteral("my-app-id"));

        // Settings hold the organization's name and nothing of the token.
        QSettings().sync();
        const QByteArray settings = readSettingsFiles();
        QVERIFY(settings.contains("Test Church"));
        QVERIFY(!settings.contains("my-secret"));
        QVERIFY(!settings.contains("my-app-id"));

        QVERIFY(pco.authorizer());
        QCOMPARE(pco.authorizer()->authorization(), server.lastAuthorization);
    }

    void aRejectedTokenIsNotSaved()
    {
        FakePlanningCenter server;
        server.status = 401;
        server.body = R"({"errors":[{"status":"401"}]})";
        PlanningCenter pco(store());
        pco.setBaseUrl(server.url());
        QSignalSpy finished(&pco, &PlanningCenter::signInFinished);

        pco.signIn("app", "wrong");
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY(!pco.signedIn());
        QVERIFY(pco.status().contains("didn't accept"));
        QVERIFY(!CredentialStore(m_service).load());
        QVERIFY(!pco.authorizer());
    }

    void anUnreachableServerIsReported()
    {
        PlanningCenter pco(store());
        pco.setBaseUrl(QUrl("http://127.0.0.1:1/services/v2/"));   // nothing listens there
        QSignalSpy finished(&pco, &PlanningCenter::signInFinished);
        pco.signIn("app", "secret");
        QVERIFY(finished.wait(20000));
        QVERIFY(!pco.signedIn());
        QVERIFY2(pco.status().contains("internet"), qPrintable(pco.status()));
        QVERIFY(!pco.busy());
    }

    void emptyFieldsAreRefusedWithoutAsking()
    {
        FakePlanningCenter server;
        PlanningCenter pco(store());
        pco.setBaseUrl(server.url());
        pco.signIn("app", "  ");
        QTest::qWait(200);
        QCOMPARE(server.requests, 0);
        QVERIFY(!pco.status().isEmpty());
    }

    void afterARestartItsStillSignedInAndCanTest()
    {
        FakePlanningCenter server;
        {
            PlanningCenter first(store());
            first.setBaseUrl(server.url());
            QSignalSpy finished(&first, &PlanningCenter::signInFinished);
            first.signIn("app", "secret");
            QVERIFY(finished.wait(5000));
        }
        PlanningCenter again(store());
        again.setBaseUrl(server.url());
        QTRY_VERIFY(again.signedIn());
        QVERIFY(again.authorizer());
        QCOMPARE(again.organization(), QStringLiteral("Test Church"));

        server.body = R"({"data":{"type":"Organization","attributes":{"name":"Renamed Church"}}})";
        QSignalSpy finished(&again, &PlanningCenter::signInFinished);
        again.testConnection();
        QVERIFY(finished.wait(5000));
        QCOMPARE(again.organization(), QStringLiteral("Renamed Church"));
    }

    void signingOutRemovesTheToken()
    {
        CredentialStore(m_service).save({"app", "secret"});
        QSettings().setValue("planningCenter/organization", "Test Church");
        PlanningCenter pco(store());
        QTRY_VERIFY(pco.signedIn());   // read from the Keychain off the GUI thread
        pco.signOut();
        QTRY_VERIFY(!pco.signedIn());
        QVERIFY(!pco.authorizer());
        QVERIFY(pco.organization().isEmpty());
        QVERIFY(!CredentialStore(m_service).load());
        QVERIFY(!QSettings().contains("planningCenter/organization"));
    }

    // #73: the old sign-in deleted the entry before adding the new one, so a failed add
    // lost the token that worked.
    void aFailedSaveKeepsTheTokenThatWorked()
    {
        QVERIFY(CredentialStore(m_service).save({"app", "old"}).ok);
        QSettings().setValue("planningCenter/organization", "Test Church");
        FakePlanningCenter server;
        PlanningCenter pco(std::make_shared<FailingSaves>(m_service));
        pco.setBaseUrl(server.url());
        QTRY_VERIFY(pco.signedIn());
        const QByteArray oldAuthorization = pco.authorizer()->authorization();

        QSignalSpy finished(&pco, &PlanningCenter::signInFinished);
        pco.signIn("app", "new");
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY2(pco.status().contains("Keychain"), qPrintable(pco.status()));
        QVERIFY(!pco.busy());
        QVERIFY(pco.signedIn());
        QCOMPARE(pco.authorizer()->authorization(), oldAuthorization);
        QCOMPARE(CredentialStore(m_service).load()->secret, QStringLiteral("old"));
    }

    // #73: a Keychain prompt or a locked keychain must not freeze the operator window.
    void theKeychainIsOnlyUsedOffTheGuiThread()
    {
        FakePlanningCenter server;
        auto recording = std::make_shared<RecordingStore>(m_service);
        PlanningCenter pco(recording);
        pco.setBaseUrl(server.url());
        QSignalSpy finished(&pco, &PlanningCenter::signInFinished);
        pco.signIn("app", "secret");
        QVERIFY(finished.wait(5000));
        QVERIFY(finished.at(0).at(0).toBool());
        // The client authorizes from memory, not the Keychain, on every request.
        const qsizetype calls = recording->threads().size();
        for (int i = 0; i < 3; ++i)
            QVERIFY(pco.authorizer());
        QCOMPARE(recording->threads().size(), calls);
        pco.signOut();
        QTRY_VERIFY(!pco.signedIn());

        const QList<QThread *> threads = recording->threads();
        QCOMPARE(threads.size(), 3);   // load at startup, save, remove
        for (QThread *t : threads)
            QVERIFY(t != QThread::currentThread());
    }

private:
    std::shared_ptr<CredentialStore> store() const { return std::make_shared<CredentialStore>(m_service); }

    QByteArray readSettingsFiles() const
    {
        QByteArray all;
        QDirIterator it(m_settingsDir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            if (f.open(QIODevice::ReadOnly))
                all += f.readAll();
        }
        return all;
    }

    QTemporaryDir m_settingsDir;
    QString m_service;
};

QTEST_GUILESS_MAIN(PlanningCenterTest)
#include "tst_planningcenter.moc"
