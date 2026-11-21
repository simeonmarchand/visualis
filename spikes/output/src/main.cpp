#include "eventlog.h"
#include "framestats.h"
#include "livestate.h"
#include "outputcontroller.h"
#include "scriptrunner.h"
#include "videoengine.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QTimer>
#include <QVideoSink>
#include <memory>

static QVideoSink *videoSinkOf(QQuickWindow *window)
{
    QObject *output = window->findChild<QObject *>("video");
    return output ? output->property("videoSink").value<QVideoSink *>() : nullptr;
}

int main(int argc, char *argv[])
{
    // The multimedia backend is chosen from the environment before the app starts.
    QString backend = "default";
    for (int i = 1; i + 1 < argc; ++i) {
        if (QByteArray(argv[i]) == "--backend") {
            backend = QString::fromLocal8Bit(argv[i + 1]);
            qputenv("QT_MEDIA_BACKEND", argv[i + 1]);
        }
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Visualis");
    QCoreApplication::setApplicationName("OutputSpike");

    QCommandLineParser parser;
    parser.setApplicationDescription("Visualis P1 output spike (see spikes/output/README.md)");
    parser.addHelpOption();
    QCommandLineOption videoOpt("video", "Background video file.", "file");
    QCommandLineOption modeOpt("video-mode", "shared | per-window", "mode", "shared");
    QCommandLineOption stageVideoOpt("stage-video", "Show the video behind the stage text too.");
    QCommandLineOption backendOpt("backend", "Qt Multimedia backend: ffmpeg | darwin", "name");
    QCommandLineOption fullscreenOpt("fullscreen", "native | borderless", "mode", "native");
    QCommandLineOption windowedOpt("windowed", "Outputs as ordinary windows (allowed on the operator screen).");
    QCommandLineOption logDirOpt("log-dir", "Log directory.", "dir");
    QCommandLineOption autoAdvanceOpt("auto-advance", "Go to the next slide every N seconds, wrapping (soak tests).", "seconds");
    QCommandLineOption scriptOpt("script", "Run a test script, save snapshots to the log dir, then quit (exit 1 on a failed check).", "file");
    parser.addOptions({videoOpt, modeOpt, stageVideoOpt, backendOpt, fullscreenOpt, windowedOpt, logDirOpt, autoAdvanceOpt, scriptOpt});
    parser.process(app);

    const QString videoMode = parser.value(modeOpt);
    if (videoMode != "shared" && videoMode != "per-window")
        qFatal("--video-mode must be shared or per-window");
    const QString fullscreen = parser.value(fullscreenOpt);
    if (fullscreen != "native" && fullscreen != "borderless")
        qFatal("--fullscreen must be native or borderless");

    const QString logDir = parser.isSet(logDirOpt)
        ? parser.value(logDirOpt)
        : QDir::currentPath() + "/spike-logs/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    EventLog::open(logDir);

    bool windowed = parser.isSet(windowedOpt);
    if (!windowed && QGuiApplication::screens().size() < 2) {
        windowed = true;
        EventLog::write("only one display: running windowed");
    }
    const bool stageVideo = parser.isSet(stageVideoOpt);
    EventLog::write(QStringLiteral("start: Qt %1, backend=%2, video-mode=%3, fullscreen=%4, windowed=%5, stage-video=%6, logs=%7")
                        .arg(qVersion(), backend, videoMode, fullscreen)
                        .arg(windowed).arg(stageVideo).arg(logDir));

    // Singletons exist before QML asks for them (see each class's create()).
    LiveState liveState;
    FrameStats frameStats(videoMode, backend);
    VideoEngine videoEngine(videoMode);
    OutputController outputs(windowed, fullscreen == "borderless" ? OutputController::FullscreenMode::Borderless
                                                                  : OutputController::FullscreenMode::Native);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Visualis.Spike", "Operator");
    if (engine.rootObjects().isEmpty())
        return 1;
    outputs.setOperatorWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));

    struct OutputSpec { QString role; QString type; bool video; };
    const QList<OutputSpec> specs{{"audience", "AudienceOutput", true}, {"stage", "StageOutput", stageVideo}};
    QList<QObject *> created;
    QMap<QString, QQuickWindow *> windowsByName{{"operator", qobject_cast<QQuickWindow *>(engine.rootObjects().first())}};
    for (const auto &spec : specs) {
        QQmlComponent component(&engine, "Visualis.Spike", spec.type);
        QObject *obj = component.createWithInitialProperties({{"showVideo", spec.video}});
        auto *window = qobject_cast<QQuickWindow *>(obj);
        if (!window) {
            qCritical().noquote() << component.errorString();
            return 1;
        }
        created << obj;
        windowsByName[spec.role] = window;
        frameStats.track(window, spec.role);
        outputs.addOutput(spec.role, window);
        if (spec.video) {
            if (QVideoSink *sink = videoSinkOf(window))
                videoEngine.addTarget(sink, spec.role);
            else
                EventLog::write("no video sink found in " + spec.type);
        }
    }

    if (parser.isSet(videoOpt))
        videoEngine.setSource(QUrl::fromLocalFile(QFileInfo(parser.value(videoOpt)).absoluteFilePath()));

    QTimer autoAdvance;
    if (parser.isSet(autoAdvanceOpt)) {
        const double seconds = parser.value(autoAdvanceOpt).toDouble();
        if (seconds <= 0)
            qFatal("--auto-advance needs a positive number of seconds");
        QObject::connect(&autoAdvance, &QTimer::timeout, &liveState, [&liveState] {
            if (liveState.index() + 1 >= liveState.slides().size())
                liveState.goTo(0);
            else
                liveState.next();
        });
        autoAdvance.start(int(seconds * 1000));
        EventLog::write(QStringLiteral("auto-advance every %1 s").arg(seconds));
    }

    std::unique_ptr<ScriptRunner> script;
    if (parser.isSet(scriptOpt)) {
        script = std::make_unique<ScriptRunner>(parser.value(scriptOpt), logDir, windowsByName);
        script->start();
    }

    const int result = app.exec();
    qDeleteAll(created);
    return result;
}
