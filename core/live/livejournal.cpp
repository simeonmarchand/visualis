#include "live/livejournal.h"

#include "service/servicejson.h"
#include "song/songjson.h"
#include "template/templatejson.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace visualis {

namespace {
constexpr int kVersion = 1;
}

QByteArray snapshotToJson(const LiveSnapshot &s)
{
    QJsonObject o{
        {"format", "visualis-live"},
        {"version", kVersion},
        {"slideIndex", s.slideIndex},
        {"textCleared", s.textCleared},
        {"backgroundCleared", s.background.cleared},
        {"backgroundTransition", transitionToJson(s.background.transition)},
        {"savedAt", s.savedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
    if (s.song) {
        o["song"] = QJsonDocument::fromJson(songToJson(*s.song)).object();
        o["arrangement"] = s.arrangement.toString(QUuid::WithoutBraces);
    }
    if (!s.background.asset.isNull())
        o["backgroundAsset"] = s.background.assetId();
    if (!s.service.isNull())
        o["service"] = s.service.toString(QUuid::WithoutBraces);
    if (!s.item.isNull())
        o["item"] = s.item.toString(QUuid::WithoutBraces);
    if (s.serviceItem)
        o["serviceItem"] = serviceItemToJson(*s.serviceItem);
    if (s.tmpl)
        o["template"] = QJsonDocument::fromJson(templateToJson(*s.tmpl)).object();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

SnapshotResult snapshotFromJson(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return {{}, QStringLiteral("not JSON: %1").arg(parseError.errorString())};
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != "visualis-live")
        return {{}, QStringLiteral("not a live state journal")};
    if (o.value("version").toInt() > kVersion)
        return {{}, QStringLiteral("written by a newer Visualis")};

    LiveSnapshot s;
    if (o.contains("song")) {
        const SongJsonResult song = songFromJson(QJsonDocument(o.value("song").toObject()).toJson());
        if (!song.song)
            return {{}, QStringLiteral("the live song can't be read: %1").arg(song.error)};
        s.song = song.song;
        s.arrangement = QUuid::fromString(o.value("arrangement").toString());
    }
    s.slideIndex = o.value("slideIndex").toInt(-1);
    s.textCleared = o.value("textCleared").toBool();
    s.background.asset = QUuid::fromString(o.value("backgroundAsset").toString());
    s.background.cleared = o.value("backgroundCleared").toBool();
    if (o.contains("backgroundTransition"))
        s.background.transition = applyTransition(s.background.transition, o.value("backgroundTransition").toObject(),
                                                  QStringLiteral("backgroundTransition"), nullptr);
    s.skippedBackgroundUrl = s.background.asset.isNull() && !o.value("background").toString().isEmpty();
    s.service = QUuid::fromString(o.value("service").toString());
    s.item = QUuid::fromString(o.value("item").toString());
    if (o.contains("serviceItem"))
        s.serviceItem = serviceItemFromJson(o.value("serviceItem").toObject());
    // A template that can't be read leaves the item on the built-in, as a missing one does.
    if (o.contains("template"))
        s.tmpl = templateFromJson(QJsonDocument(o.value("template").toObject()).toJson()).tmpl;
    s.savedAt = QDateTime::fromString(o.value("savedAt").toString(), Qt::ISODateWithMs);
    return {s, {}};
}

bool writeJournal(const QString &path, const LiveSnapshot &snapshot, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(snapshotToJson(snapshot)) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

SnapshotResult readJournal(const QString &path)
{
    QFile file(path);
    if (!file.exists())
        return {{}, {}};
    if (!file.open(QIODevice::ReadOnly))
        return {{}, file.errorString()};
    // A journal is a few KB; anything far bigger isn't one.
    if (file.size() > 4 * 1024 * 1024)
        return {{}, QStringLiteral("too large to be a live state journal")};
    return snapshotFromJson(file.readAll());
}

} // namespace visualis
