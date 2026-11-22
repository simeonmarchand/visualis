// Reads the next upcoming plan from the real Planning Center API through PcoClient (#53)
// and prints its shape, never its content. Not a CTest test: a normal test run must never
// call the real API. Run by hand with a token in the environment:
//
//   set -a; . ~/.config/visualis/pco.env; set +a
//   ./build/debug/tests/pco_live_sandbox
#include "pco/pcoclient.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QTimer>

#include <cstdio>

using namespace visualis;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const PcoCredentials creds{qEnvironmentVariable("PCO_APP_ID"), qEnvironmentVariable("PCO_SECRET")};
    if (!creds.isValid()) {
        std::fprintf(stderr, "set PCO_APP_ID and PCO_SECRET (see the comment at the top of this file)\n");
        return 2;
    }
    QNetworkAccessManager network;
    PcoClient pco(&network, QUrl(QString::fromLatin1(kPcoServicesApi)),
                  [creds] { return std::make_unique<PersonalAccessToken>(creds); });
    const auto fail = [](const PcoError &e) {
        std::fprintf(stderr, "failed: %s\n", qPrintable(e.message));
        QCoreApplication::exit(1);
    };
    QTimer::singleShot(120'000, &app, [] {
        std::fprintf(stderr, "timed out\n");
        QCoreApplication::exit(1);
    });

    // Service types in turn until one has an upcoming plan.
    auto types = std::make_shared<QList<PcoServiceType>>();
    auto tryNext = std::make_shared<std::function<void()>>();
    *tryNext = [&pco, &fail, types, tryNext] {
        if (types->isEmpty()) {
            std::printf("no upcoming plans for this token\n");
            return QCoreApplication::exit(0);
        }
        const PcoServiceType type = types->takeFirst();
        pco.futurePlans(type.id, [&pco, &fail, type, tryNext](PcoResult<QList<PcoPlan>> plans) {
            if (!plans.ok())
                return fail(plans.error);
            if (plans.value.isEmpty())
                return (*tryNext)();
            const PcoPlan plan = plans.value.first();
            pco.planItems(type.id, plan.id, [&pco, &fail, plan](PcoResult<PcoPlanItems> items) {
                if (!items.ok())
                    return fail(items.error);
                int songs = 0, withSequence = 0;
                for (const PcoItem &i : items.value.items) {
                    songs += i.type == PcoItemType::Song;
                    withSequence += !i.sequence().isEmpty();
                }
                const bool matches = items.value.items.size() == plan.itemsCount;
                std::printf("plan %s: %lld items (plan says %d), %d songs, %d with a sequence, "
                            "%lld skipped, link %s, %d requests\n",
                            qPrintable(plan.id), qlonglong(items.value.items.size()), plan.itemsCount, songs,
                            withSequence, qlonglong(items.value.skipped.size()),
                            plan.planningCenterUrl.isValid() ? "ok" : "missing", pco.requestCount());
                QCoreApplication::exit(matches ? 0 : 1);
            });
        });
    };
    pco.serviceTypes([&fail, types, tryNext](PcoResult<QList<PcoServiceType>> r) {
        if (!r.ok())
            return fail(r.error);
        *types = r.value;
        (*tryNext)();
    });
    const int code = app.exec();
    *tryNext = nullptr;   // break the self-reference
    return code;
}
