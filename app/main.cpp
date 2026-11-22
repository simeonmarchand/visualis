#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "live/commandsqml.h"
#include "live/livestate.h"
#include "live/recovery.h"
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

    // Test options, used by scripts/hotplug-test.sh, scripts/crash-test.sh, and soak runs.
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption settingsDirOpt("settings-dir",
        "Keep settings in an INI file in this directory instead of the user's preferences.", "dir");
    const QCommandLineOption assignOpt("assign",
        "Put an output on the display with this name, e.g. stage=\"Fake Stage\". Repeatable.", "role=name");
    const QCommandLineOption backgroundOpt("background", "Play this video as the background.", "file");
    const QCommandLineOption libraryOpt("library",
        "Use the song library in this folder instead of ~/Documents/Visualis.", "dir");
    const QCommandLineOption journalOpt("journal",
        "Keep the crash-recovery journal in this file instead of Application Support.", "file");
    const QCommandLineOption commandOpt("command",
        "Run an operator command after startup, e.g. next_slide or goto_slide=2. Repeatable.", "name[=arg]");
    const QCommandLineOption importOpt("import",
        "Import the Visualis song and service exports in this folder, and open the last service.", "dir");
    parser.addOptions({settingsDirOpt, assignOpt, backgroundOpt, libraryOpt, journalOpt, commandOpt, importOpt});
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
    auto *songs = engine.singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
    auto *services = engine.singletonInstance<ServiceList *>("Visualis.Ui", "ServiceList");
    if (songs)
        songs->openLibrary(parser.isSet(libraryOpt) ? parser.value(libraryOpt) : SongLibrary::defaultFolder());
    if (parser.isSet(importOpt) && songs && services) {
        const QUrl folder = QUrl::fromLocalFile(QFileInfo(parser.value(importOpt)).absoluteFilePath());
        songs->importSongs(folder);   // first, so the services' songs are there
        services->importServices(folder);
    }

    // Back to what was live if the last run crashed (#25), then journal every change.
    auto *live = engine.singletonInstance<visualis::LiveState *>("Visualis.Ui", "LiveState");
    auto *video = engine.singletonInstance<VideoEngine *>("Visualis.Ui", "VideoEngine");
    if (auto *recovery = engine.singletonInstance<Recovery *>("Visualis.Ui", "Recovery"); recovery && live) {
        const QString journal = parser.isSet(journalOpt) ? parser.value(journalOpt) : Recovery::defaultJournalPath();
        if (recovery->start(journal, live, video)) {
            // The operator sees its slides: the service item it was, else the library's song.
            if (!live->serviceIdString().isEmpty() && services && services->openId() != live->serviceIdString())
                services->openService(live->serviceIdString());
            if (services && live->itemIndex() >= 0)
                services->select(live->itemIndex());
            else if (songs && !live->songId().isEmpty())
                songs->select(live->songId());
        }
    }

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
    if (parser.isSet(backgroundOpt) && video)
        video->setSource(QUrl::fromLocalFile(QFileInfo(parser.value(backgroundOpt)).absoluteFilePath()));
    if (auto *commands = engine.singletonInstance<Commands *>("Visualis.Ui", "Commands")) {
        for (const QString &c : parser.values(commandOpt))
            commands->run(c.section('=', 0, 0), c.contains('=') ? QVariant(c.section('=', 1)) : QVariant());
    }
    return app.exec();
}
