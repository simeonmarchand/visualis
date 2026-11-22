#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// What the Planning Center client reads (#53), as plain values, so the import (#54) can be
// tested without a network. Field names follow the Services API; the comments note what
// the P3 spike learned about them (spikes/planning-center/README.md).
namespace visualis {

struct PcoServiceType
{
    QString id;
    QString name;
};

struct PcoPlan
{
    QString id;
    QString serviceTypeId;
    QString title;              // often empty
    QString seriesTitle;        // often empty
    QString dates;              // for display, e.g. "January 10, 2027"
    QDateTime sortDate;         // UTC
    int itemsCount = 0;
    QUrl planningCenterUrl;     // "open in Planning Center" (P3 finding 7)
};

struct PcoSong
{
    QString id;
    QString title;
    QString ccliNumber;         // empty when the song has none
    QString author;
    QString copyright;          // trimmed (P3 finding 8)
};

struct PcoArrangement
{
    QString id;
    QString name;
    QStringList sequence;       // the full song map, repeats included (P3 finding 9)
    int bpm = 0;                // 0 when unknown
    QString meter;
    int lengthSeconds = 0;
};

struct PcoKey
{
    QString id;
    QString name;
    QString startingKey;
};

// P3 finding 3. Anything Planning Center adds later reads as Item.
enum class PcoItemType { Item, Song, Header, Media };
enum class PcoServicePosition { During, Pre, Post };

struct PcoItem
{
    QString id;
    PcoItemType type = PcoItemType::Item;
    QString title;
    int position = 0;           // the API's `sequence`: order within the plan
    PcoServicePosition servicePosition = PcoServicePosition::During;
    int lengthSeconds = 0;
    QString description;
    QString htmlDetails;
    QString keyName;            // the key chosen on the item, if any
    // This service's section order, when it differs from the arrangement's (P3 finding 4).
    QStringList customArrangementSequence;
    std::optional<PcoSong> song;
    std::optional<PcoArrangement> arrangement;
    std::optional<PcoKey> key;

    // The order to sing the sections in today: the item's own order if it has one.
    QStringList sequence() const
    {
        if (!customArrangementSequence.isEmpty())
            return customArrangementSequence;
        return arrangement ? arrangement->sequence : QStringList();
    }
    // The key to show: the item's choice, else the key record's name or starting key.
    QString keyLabel() const
    {
        if (!keyName.isEmpty())
            return keyName;
        if (!key)
            return {};
        return key->name.isEmpty() ? key->startingKey : key->name;
    }
};

// A plan with its items in plan order.
struct PcoPlanItems
{
    QList<PcoItem> items;
    QStringList skipped;        // records left out or only partly read; also in PcoResult::skipped
    // For re-import (#56): the ids of items left out or only partly read, so a merge can
    // leave the operator's copies alone, and whether a record was left out without an id
    // (then any item missing from `items` may only have been unreadable).
    QStringList unreadableIds;
    bool skippedWithoutId = false;
};

// Why a read failed. The message is written for the operator.
struct PcoError
{
    enum Kind { None, Offline, Unauthorized, Forbidden, NotFound, RateLimited, Server, Malformed, NotSignedIn, Other };
    Kind kind = None;
    int httpStatus = 0;
    QString message;

    explicit operator bool() const { return kind != None; }
};

// A value, or why there isn't one.
template <typename T>
struct PcoResult
{
    T value{};
    PcoError error;
    // Records that were left out or only partly read, for the operator (set on success).
    QStringList skipped;

    bool ok() const { return !error; }
};

} // namespace visualis
