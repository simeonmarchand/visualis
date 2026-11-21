#include "output/outputcontroller.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QSettings>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Visualis");
    QCoreApplication::setApplicationName("Visualis");

    // Test options, used by scripts/hotplug-test.sh.
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption settingsDirOpt("settings-dir",
        "Keep settings in an INI file in this directory instead of the user's preferences.", "dir");
    const QCommandLineOption assignOpt("assign",
        "Put an output on the display with this name, e.g. stage=\"Fake Stage\". Repeatable.", "role=name");
    parser.addOptions({settingsDirOpt, assignOpt});
    parser.process(app);

    if (parser.isSet(settingsDirOpt)) {
        // Before anything creates a QSettings.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, parser.value(settingsDirOpt));
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Visualis.Ui", "Main");

    if (auto *outputs = engine.singletonInstance<OutputController *>("Visualis.Ui", "OutputController")) {
        for (const QString &a : parser.values(assignOpt)) {
            const QString role = a.section('=', 0, 0);
            const QString name = a.section('=', 1);
            bool found = false;
            for (const QVariant &d : outputs->displays()) {
                if (d.toMap().value("label").toString().startsWith(name + " (")) {
                    outputs->assign(role, d.toMap().value("key").toString());
                    found = true;
                }
            }
            if (!found)
                qCWarning(lcOutput).noquote() << "--assign: no display named" << name;
        }
    }
    return app.exec();
}
