#pragma once

#include "pco/pcotypes.h"
#include "service/service.h"

#include <QDateTime>
#include <QList>
#include <QStringList>

namespace visualis {

// Turns a Planning Center plan into a Visualis service (#54, plan 5.3). Pure, so it's
// tested without a network; the app's importer fetches the plan and saves the result.
//
//  - Items keep Planning Center's order and their pre / during / post position.
//  - Headers stay headers. Media items become media placeholders (no file yet).
//  - Songs are placeholders until song matching (#55) links them to library songs. The
//    order to sing them in is kept as `customSequence`: the item's own order if it has
//    one, else the arrangement's, with every lyric-less section (Intro, Ending) kept.
//  - A plain item whose title holds a scripture reference ("Scripture Reading: Acts 2:38")
//    becomes a scripture item with the reference as its body, for Phase 4 to fill.
//  - Other plain items (Welcome, Sermon) are placeholders with their title.
//  - Every item keeps its `pcoItemId` and a snapshot of what Planning Center said, for
//    re-import (#56).
struct PlanImport {
    Service service;
    QStringList notes;     // what the operator should know: records that couldn't be read
};

PlanImport serviceFromPlan(const PcoServiceType &type, const PcoPlan &plan, const PcoPlanItems &items,
                           const QDateTime &importedAt);

// What's kept of an item in `ServiceItem::importSnapshot`. Only the plan's content (titles,
// order, songs, keys); never people or notes (plan 7.2).
QJsonObject importSnapshot(const PcoItem &item);

// Plans for the picker: upcoming ones first, soonest first, then past ones, latest first.
// A plan counts as upcoming through the whole of its day (`today` is the operator's date).
QList<PcoPlan> plansForPicker(QList<PcoPlan> plans, const QDate &today);

} // namespace visualis
