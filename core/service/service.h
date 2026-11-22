#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <optional>

namespace visualis {

// One entry in a service's order (plan 5.2).
struct ServiceItem {
    enum class Kind { Header, Song, Scripture, Presentation, Media, Text, Placeholder };

    QUuid id;
    Kind kind = Kind::Placeholder;
    QString title;              // the header text, the song's title when added, a scripture reference...
    QUuid ref;                  // what it shows: the song id for songs (media and presentations later)
    QUuid arrangement;          // songs: this service's arrangement; null for the song's default
    QStringList customSequence; // songs: Planning Center's group order for this day only (P3 finding 4)
    QString body;               // text items: the text; scripture: the reference as written
    QString servicePosition;    // Planning Center's "pre", "during", or "post"; empty for during
    QString pcoItemId;          // Planning Center link, if imported
    QJsonObject overrides;      // template, background, slide edits (plan 5.2; shaped in Phase 3)
    QJsonObject importSnapshot; // Planning Center's fields at the last import, for re-import
    bool pcoRemoved = false;    // gone from the plan at the last re-import: flagged, not deleted (#56)

    bool operator==(const ServiceItem &) const = default;
};

// An imported item the operator deleted (#56): a re-import doesn't bring it back unless
// they ask. `importSnapshot` is what Planning Center said when it was last merged.
struct RemovedPcoItem {
    QString pcoItemId;
    QJsonObject importSnapshot;

    bool operator==(const RemovedPcoItem &) const = default;
};

struct Service {
    QUuid id;                   // null until first saved
    QString title;
    QDate date;
    QString pcoPlanId;
    QString pcoServiceTypeId;
    QString pcoPlanUrl;         // the plan in Planning Center's web app (P3 finding 7)
    QDateTime lastImportedAt;   // UTC; invalid if never imported
    QList<ServiceItem> items;   // in order
    QList<RemovedPcoItem> removedPcoItems;   // kept by ServiceStore when an imported item is deleted
    QDateTime created;          // set by ServiceStore, UTC
    QDateTime updated;

    bool operator==(const Service &) const = default;
};

// Kind names as stored and exported: "header", "song", "scripture", ...
QString kindName(ServiceItem::Kind kind);
std::optional<ServiceItem::Kind> kindFromName(const QString &name);

} // namespace visualis
