#include "pco/planimport.h"

#include "scripture/reference.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace visualis {

namespace {

QString positionName(PcoServicePosition p)
{
    switch (p) {
    case PcoServicePosition::Pre: return QStringLiteral("pre");
    case PcoServicePosition::Post: return QStringLiteral("post");
    case PcoServicePosition::During: break;
    }
    return {};   // during is the default (ServiceItem::servicePosition)
}

QString typeName(PcoItemType t)
{
    switch (t) {
    case PcoItemType::Song: return QStringLiteral("song");
    case PcoItemType::Header: return QStringLiteral("header");
    case PcoItemType::Media: return QStringLiteral("media");
    case PcoItemType::Item: break;
    }
    return QStringLiteral("item");
}

// The plan's date where the church is: a 7 pm service is still that day, though it's the
// next day in UTC.
QDate planDate(const PcoPlan &plan) { return plan.sortDate.toLocalTime().date(); }

} // namespace

QJsonObject importSnapshot(const PcoItem &item)
{
    QJsonObject o{
        {"itemType", typeName(item.type)}, {"title", item.title}, {"position", item.position},
        {"servicePosition", positionName(item.servicePosition)}, {"lengthSeconds", item.lengthSeconds},
        {"description", item.description}, {"key", item.keyLabel()},
        {"customArrangementSequence", QJsonArray::fromStringList(item.customArrangementSequence)},
    };
    if (item.song) {
        // Credits as SongSelect wrote them, trimmed (P3 finding 8).
        const PcoSong &s = *item.song;
        o["song"] = QJsonObject{{"id", s.id}, {"title", s.title.trimmed()}, {"ccliNumber", s.ccliNumber.trimmed()},
                                {"author", s.author.trimmed()}, {"copyright", s.copyright.trimmed()}};
    }
    if (item.arrangement) {
        const PcoArrangement &a = *item.arrangement;
        o["arrangement"] = QJsonObject{{"id", a.id}, {"name", a.name}, {"sequence", QJsonArray::fromStringList(a.sequence)},
                                       {"bpm", a.bpm}, {"meter", a.meter}, {"lengthSeconds", a.lengthSeconds}};
    }
    return o;
}

ServiceItem importedItem(const QString &pcoItemId, const QJsonObject &snapshot)
{
    const auto strings = [](const QJsonValue &v) {
        QStringList out;
        for (const QJsonValue &label : v.toArray())
            out << label.toString();
        return out;
    };
    ServiceItem it;
    it.title = snapshot.value("title").toString().trimmed();
    it.servicePosition = snapshot.value("servicePosition").toString();
    it.pcoItemId = pcoItemId;
    it.importSnapshot = snapshot;
    const QString type = snapshot.value("itemType").toString();
    if (type == QLatin1String("header")) {
        it.kind = ServiceItem::Kind::Header;
    } else if (type == QLatin1String("media")) {
        it.kind = ServiceItem::Kind::Media;   // no file yet: `ref` stays null
    } else if (type == QLatin1String("song")) {
        it.kind = ServiceItem::Kind::Placeholder;
        if (it.title.isEmpty())
            it.title = snapshot.value("song").toObject().value("title").toString();
        it.customSequence = strings(snapshot.value("customArrangementSequence"));
        if (it.customSequence.isEmpty())
            it.customSequence = strings(snapshot.value("arrangement").toObject().value("sequence"));
    } else if (const auto ref = findScriptureReference(it.title)) {
        it.kind = ServiceItem::Kind::Scripture;
        it.body = ref->reference;
    } else {
        it.kind = ServiceItem::Kind::Placeholder;
    }
    return it;
}

PlanImport serviceFromPlan(const PcoServiceType &type, const PcoPlan &plan, const PcoPlanItems &items,
                           const QDateTime &importedAt)
{
    PlanImport r;
    Service &s = r.service;
    for (const QString &title : {plan.title, plan.seriesTitle, type.name, plan.dates}) {
        if (!title.trimmed().isEmpty()) {
            s.title = title.trimmed();
            break;
        }
    }
    if (s.title.isEmpty())
        s.title = QStringLiteral("Planning Center plan %1").arg(plan.id);
    s.date = planDate(plan);
    s.pcoPlanId = plan.id;
    s.pcoServiceTypeId = plan.serviceTypeId.isEmpty() ? type.id : plan.serviceTypeId;
    s.pcoPlanUrl = plan.planningCenterUrl.toString();
    s.lastImportedAt = importedAt.toUTC();

    QList<PcoItem> ordered = items.items;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const PcoItem &a, const PcoItem &b) { return a.position < b.position; });

    for (const PcoItem &p : std::as_const(ordered))
        s.items << importedItem(p.id, importSnapshot(p));
    for (const QString &skipped : items.skipped)
        r.notes << QStringLiteral("Skipped from Planning Center: %1").arg(skipped);
    return r;
}

QList<PcoPlan> plansForPicker(QList<PcoPlan> plans, const QDate &today)
{
    const auto upcoming = [&](const PcoPlan &p) { return planDate(p) >= today; };
    std::stable_sort(plans.begin(), plans.end(), [&](const PcoPlan &a, const PcoPlan &b) {
        if (upcoming(a) != upcoming(b))
            return upcoming(a);
        return upcoming(a) ? a.sortDate < b.sortDate : a.sortDate > b.sortDate;
    });
    return plans;
}

} // namespace visualis
