#pragma once

// Plans for the re-import tests (#56): synthetic items, public-domain titles, made-up ids.

#include "pco/planimport.h"
#include "pco/reimport.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace visualis::testing {

using Kind = ServiceItem::Kind;
using CK = ReimportChange::Kind;

inline PcoItem song(const QString &id, const QString &title, QStringList sequence = {"Verse 1", "Chorus"})
{
    PcoItem it;
    it.id = id;
    it.type = PcoItemType::Song;
    it.title = title;
    it.song = PcoSong{"s" + id, title, {}, "Traditional", "Public Domain"};
    it.arrangement = PcoArrangement{"a" + id, "Default", std::move(sequence), 0, {}, 0};
    return it;
}

inline PcoItem plain(const QString &id, const QString &title, PcoItemType type = PcoItemType::Item)
{
    PcoItem it;
    it.id = id;
    it.type = type;
    it.title = title;
    return it;
}

// Planning Center numbers items in plan order.
inline QList<PcoItem> numbered(QList<PcoItem> items)
{
    for (int i = 0; i < items.size(); ++i)
        items[i].position = (i + 1) * 10;
    return items;
}

inline Service import(const QList<PcoItem> &items)
{
    PcoPlanItems p;
    p.items = numbered(items);
    PcoPlan plan;
    plan.id = "70001";
    return serviceFromPlan({"900", "Sunday"}, plan, p, QDateTime::currentDateTimeUtc()).service;
}

// The operator's copy: saved, so every item has an id.
inline Service local(const QList<PcoItem> &items)
{
    Service s = import(items);
    s.id = QUuid::createUuid();
    for (ServiceItem &it : s.items)
        it.id = QUuid::createUuid();
    return s;
}

inline QStringList titles(const Service &s)
{
    QStringList out;
    for (const ServiceItem &it : s.items)
        out << it.title;
    return out;
}

inline const ServiceItem *byPco(const Service &s, const QString &pcoId)
{
    for (const ServiceItem &it : s.items) {
        if (it.pcoItemId == pcoId)
            return &it;
    }
    return nullptr;
}

inline QList<CK> kinds(const ReimportResult &r)
{
    QList<CK> out;
    for (const ReimportChange &c : r.changes)
        out << c.kind;
    return out;
}

} // namespace visualis::testing
