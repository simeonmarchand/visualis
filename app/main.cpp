#include "library/songlibrary.h"
#include "output/outputcontroller.h"
#include "video/videoengine.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QSettings>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // AVFoundation, not Qt's default FFmpeg backend: it paced ProRes cleanly where FFmpeg
    // didn't, and uses Apple's hardware decoders (ADR 0001). Must be set before Qt Multimedia starts.
    if (!qEnvironmentVariableIsSet("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "darwin");
#endif
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Visualis");
    QCoreApplication::setApplicationName("Visualis");

    // Test options, used by scripts/hotplug-test.sh and soak runs.
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption settingsDirOpt("settings-dir",
        "Keep settings in an INI file in this directory instead of the user's preferences.", "dir");
    const QCommandLineOption assignOpt("assign",
        "Put an output on the display with this name, e.g. stage=\"Fake Stage\". Repeatable.", "role=name");
    const QCommandLineOption backgroundOpt("background", "Play this video as the background.", "file");
    const QCommandLineOption libraryOpt("library",
        "Use the song library in this folder instead of ~/Documents/Visualis.", "dir");
    parser.addOptions({settingsDirOpt, assignOpt, backgroundOpt, libraryOpt});
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

    // After the window is up, so a library that can't be opened shows its error there.
    if (auto *songs = engine.singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary"))
        songs->openLibrary(parser.isSet(libraryOpt) ? parser.value(libraryOpt) : SongLibrary::defaultFolder());

    if (auto *outputs = engine.singletonInstance<OutputController *>("Visualis.Ui", "OutputController")) {
        for (const QString &a : parser.values(assignOpt)) {
            const QString role = a.section('=', 0, 0);
            const QString name = a.section('=', 1);
            bool found = false;
            for (const QVariant &d : outputs->displays()) {
                if (d.toMap().value("label").toString().startsWith(name + " (")) {
                    outputs->assign(role, d.toMap().value("ref").toString());
                    found = true;
                }
            }
            if (!found)
                qCWarning(lcOutput).noquote() << "--assign: no display named" << name;
        }
    }
    if (parser.isSet(backgroundOpt)) {
        if (auto *video = engine.singletonInstance<VideoEngine *>("Visualis.Ui", "VideoEngine"))
            video->setSource(QUrl::fromLocalFile(QFileInfo(parser.value(backgroundOpt)).absoluteFilePath()));
    }
    return app.exec();
}
