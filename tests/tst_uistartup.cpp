#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

// Catches QML that fails to load (syntax errors, missing imports, bad bindings) before a
// person ever opens the app.
class UiStartup : public QObject
{
    Q_OBJECT

private slots:
    void operatorWindowLoadsWithoutWarnings()
    {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &w) { warnings += w; });

        engine.loadFromModule("Visualis.Ui", "Main");

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->objectName(), QStringLiteral("operatorWindow"));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.value(0).toString()));
    }

    // The offscreen platform has one display, which the operator window is on, so both
    // outputs must stay hidden rather than cover it.
    void outputsNeverCoverTheOperatorScreen()
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("Visualis.Ui", "Main");
        auto *root = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(root);
        QVERIFY(QTest::qWaitForWindowExposed(root));

        for (const char *name : {"audienceOutput", "stageOutput"}) {
            auto *output = root->findChild<QQuickWindow *>(name);
            QVERIFY2(output, name);
            QVERIFY2(!output->isVisible(), name);
        }
        // Nothing showing, so nothing holds the displays awake (#26).
        auto *outputs = engine.singletonInstance<QObject *>("Visualis.Ui", "OutputController");
        QVERIFY(outputs);
        QVERIFY(!outputs->property("keepingDisplaysAwake").toBool());
    }
};

QTEST_MAIN(UiStartup)
#include "tst_uistartup.moc"
