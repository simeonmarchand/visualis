#include "live/commandsqml.h"

#include "live/commands.h"
#include "live/livestate.h"
#include "output/outputcontroller.h"

#include <QLoggingCategory>
#include <QQmlEngine>

Q_LOGGING_CATEGORY(lcCommands, "visualis.commands")

bool Commands::run(const QString &name, const QVariant &arg)
{
    QQmlEngine *engine = qmlEngine(this);
    auto *live = engine ? engine->singletonInstance<visualis::LiveState *>("Visualis.Ui", "LiveState") : nullptr;
    if (!live)
        return false;
    if (auto *outputs = engine->singletonInstance<OutputController *>("Visualis.Ui", "OutputController"))
        outputs->markCommand(name);
    const bool ok = visualis::runCommand(*live, name, arg);
    qCInfo(lcCommands).noquote() << name << (arg.isValid() ? arg.toString() : QString())
                                 << (ok ? "" : "(nothing to do)");
    return ok;
}
