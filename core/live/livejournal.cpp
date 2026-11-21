#include "live/livejournal.h"

#include "song/songjson.h"

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
        {"backgroundCleared", s.backgroundCleared},
        {"background", s.background},
        {"savedAt", s.savedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
    if (s.song) {
        o["song"] = QJsonDocument::fromJson(songToJson(*s.song)).object();
        o["arrangement"] = s.arrangement.toString(QUuid::WithoutBraces);
    }
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
    s.backgroundCleared = o.value("backgroundCleared").toBool();
    s.background = o.value("background").toString();
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
