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
};

QTEST_MAIN(UiStartup)
#include "tst_uistartup.moc"
