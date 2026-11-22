#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace visualis {

// A labeled, colored block of slides: what ProPresenter calls a group (plan 5.2 "Groups").
struct Group {
    QUuid id;
    QString label;        // "Verse 1", "Chorus"; may be empty
    QString color;        // "#rrggbb", or empty for no color
    QStringList slides;   // one entry per slide; an empty string is a blank slide (Intro, Interlude)
    // Per-slide template overrides (template.h), parallel to `slides`; {} for none. SongStore
    // keeps it exactly as long as `slides`. Editing the text must remap it (#95).
    QList<QJsonObject> slideOverrides;

    bool operator==(const Group &) const = default;
};

// An order to play a song's groups in. A group may appear more than once.
struct Arrangement {
    QUuid id;
    QString name;
    QList<QUuid> sequence;       // group ids
    QString pcoArrangementId;    // Planning Center link, if imported

    bool operator==(const Arrangement &) const = default;
};

// Optional. A church that doesn't use CCLI leaves these empty (plan 5.2).
struct Credits {
    QString ccliNumber;
    QString authors;
    QString copyright;

    bool operator==(const Credits &) const = default;
};

struct Song {
    QUuid id;                       // null until first saved
    QString title;
    Credits credits;
    QString pcoSongId;              // Planning Center link, if imported
    QList<Group> groups;            // in display order
    QList<Arrangement> arrangements;
    QUuid defaultArrangement;       // null: the first arrangement, or groups in order
    QUuid defaultTemplate;          // null: the built-in for the kind (plan 5.2)
    QDateTime created;              // set by SongStore, UTC
    QDateTime updated;

    bool operator==(const Song &) const = default;

    const Group *group(const QUuid &id) const;
    Group *group(const QUuid &id);
    const Arrangement *arrangement(const QUuid &id) const;

    // Removes a group and every arrangement step that uses it.
    void removeGroup(const QUuid &id);

    // The group ids to play, in order: the given arrangement, else the default arrangement,
    // else the first one, else every group in display order (songs with no arrangement).
    QList<QUuid> playOrder(const QUuid &arrangement = {}) const;
};

} // namespace visualis
