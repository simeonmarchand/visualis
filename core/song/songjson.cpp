#include "songjson.h"
#include "songstore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace visualis {

namespace {

constexpr int kVersion = 1;
const char *kFormat = "visualis-song";
const char *kSuffix = ".visualis-song.json";
constexpr int kMaxStem = 120;   // keeps the whole name well under every file system's limit

QString idText(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }
QJsonValue idOrNull(const QUuid &id) { return id.isNull() ? QJsonValue() : QJsonValue(idText(id)); }
QString time(const QDateTime &t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString(); }

// A group reference in a sequence: its id, or (in hand-written files) its label.
std::optional<QUuid> resolveGroup(const QString &ref, const QList<Group> &groups, QString *error)
{
    const QUuid asId = QUuid::fromString(ref);
    for (const Group &g : groups) {
        if (!asId.isNull() && g.id == asId)
            return g.id;
    }
    QList<QUuid> byLabel;
    for (const Group &g : groups) {
        if (g.label.compare(ref, Qt::CaseInsensitive) == 0)
            byLabel << g.id;
    }
    if (byLabel.size() == 1)
        return byLabel.first();
    *error = byLabel.isEmpty() ? QStringLiteral("no group \"%1\"").arg(ref)
                               : QStringLiteral("more than one group is labelled \"%1\"").arg(ref);
    return std::nullopt;
}

} // namespace

QByteArray songToJson(const Song &s)
{
    QJsonArray groups;
    for (const Group &g : s.groups) {
        groups.append(QJsonObject{{"id", idText(g.id)}, {"label", g.label}, {"color", g.color},
                                  {"slides", QJsonArray::fromStringList(g.slides)}});
    }
    QJsonArray arrangements;
    for (const Arrangement &a : s.arrangements) {
        QJsonArray seq;
        for (const QUuid &id : a.sequence)
            seq.append(idText(id));
        arrangements.append(QJsonObject{{"id", idText(a.id)}, {"name", a.name}, {"sequence", seq},
                                        {"pcoArrangementId", a.pcoArrangementId}});
    }
    QJsonObject o{
        {"format", kFormat},
        {"version", kVersion},
        {"id", idOrNull(s.id)},
        {"title", s.title},
        {"credits", QJsonObject{{"ccliNumber", s.credits.ccliNumber}, {"authors", s.credits.authors},
                                {"copyright", s.credits.copyright}}},
        {"pcoSongId", s.pcoSongId},
        {"groups", groups},
        {"arrangements", arrangements},
        {"defaultArrangement", idOrNull(s.defaultArrangement)},
    };
    if (s.created.isValid())
        o["created"] = time(s.created);
    if (s.updated.isValid())
        o["updated"] = time(s.updated);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

SongJsonResult songFromJson(const QByteArray &json)
{
    SongJsonResult r;
    auto fail = [&](const QString &why) {
        r.song.reset();
        r.error = why;
        return r;
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not valid JSON (%1 at character %2)").arg(parseError.errorString()).arg(parseError.offset));
    if (!doc.isObject())
        return fail(QStringLiteral("not a Visualis song file"));
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != kFormat)
        return fail(QStringLiteral("not a Visualis song file (format is \"%1\")").arg(o.value("format").toString()));
    const int version = o.value("version").toInt(0);
    if (version < 1)
        return fail(QStringLiteral("the file has no valid version"));
    if (version > kVersion)
        return fail(QStringLiteral("made by a newer Visualis (song format %1; this version reads up to %2)")
                        .arg(version).arg(kVersion));

    Song s;
    s.id = QUuid::fromString(o.value("id").toString());
    s.title = o.value("title").toString().trimmed();
    if (s.title.isEmpty())
        return fail(QStringLiteral("the song has no title"));
    const QJsonObject credits = o.value("credits").toObject();
    s.credits = {credits.value("ccliNumber").toString(), credits.value("authors").toString(),
                 credits.value("copyright").toString()};
    s.pcoSongId = o.value("pcoSongId").toString();
    s.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODateWithMs);
    s.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODateWithMs);

    QSet<QUuid> seen;
    for (const QJsonValue &v : o.value("groups").toArray()) {
        const QJsonObject g = v.toObject();
        QUuid id = QUuid::fromString(g.value("id").toString());
        if (id.isNull())
            id = QUuid::createUuid();   // hand-written files may leave ids out
        if (seen.contains(id))
            return fail(QStringLiteral("two groups have the id %1").arg(idText(id)));
        seen.insert(id);
        QStringList slides;
        for (const QJsonValue &slide : g.value("slides").toArray())
            slides << slide.toString();
        s.groups << Group{id, g.value("label").toString(), g.value("color").toString(), slides};
    }

    for (const QJsonValue &v : o.value("arrangements").toArray()) {
        const QJsonObject a = v.toObject();
        Arrangement arr{QUuid::fromString(a.value("id").toString()), a.value("name").toString(), {},
                        a.value("pcoArrangementId").toString()};
        if (arr.id.isNull())
            arr.id = QUuid::createUuid();
        for (const QJsonValue &step : a.value("sequence").toArray()) {
            QString why;
            const std::optional<QUuid> id = resolveGroup(step.toString(), s.groups, &why);
            if (!id)
                return fail(QStringLiteral("arrangement \"%1\": %2").arg(arr.name, why));
            arr.sequence << *id;
        }
        s.arrangements << arr;
    }

    const QString def = o.value("defaultArrangement").toString();
    if (!def.isEmpty()) {
        const QUuid asId = QUuid::fromString(def);
        for (const Arrangement &a : std::as_const(s.arrangements)) {
            if ((!asId.isNull() && a.id == asId) || a.name.compare(def, Qt::CaseInsensitive) == 0) {
                s.defaultArrangement = a.id;
                break;
            }
        }
        if (s.defaultArrangement.isNull())
            return fail(QStringLiteral("the default arrangement \"%1\" isn't one of the song's arrangements").arg(def));
    }
    r.song = s;
    return r;
}

QString songFileName(const QString &title)
{
    // Replace what macOS, Windows, or Linux won't accept in a file name.
    static const QRegularExpression unsafe(QStringLiteral(R"([/\\:*?"<>|\x00-\x1f])"));
    QString name = QString(title).replace(unsafe, "-").simplified();
    while (name.startsWith('.'))
        name.remove(0, 1);
    if (name.isEmpty())
        name = QStringLiteral("Untitled");
    return name.left(kMaxStem) + kSuffix;
}

SongExportReport exportSongs(SongStore &store, const QString &folder)
{
    SongExportReport report;
    QDir().mkpath(folder);
    QSet<QString> used;
    for (const SongSummary &summary : store.list()) {
        const std::optional<Song> song = store.load(summary.id);
        if (!song) {
            report.failed << std::pair{summary.title, QStringLiteral("couldn't be read from the library")};
            continue;
        }
        QString name = songFileName(song->title);
        // Shorten the stem to make room for " (n)", so the length limit can't cut it off.
        const QString stem = name.chopped(qsizetype(qstrlen(kSuffix)));
        for (int n = 2; used.contains(name.toLower()); ++n) {
            const QString tag = QStringLiteral(" (%1)").arg(n);
            name = stem.left(kMaxStem - tag.size()) + tag + kSuffix;
        }
        used.insert(name.toLower());

        QSaveFile f(QDir(folder).filePath(name));   // written whole or not at all
        if (!f.open(QIODevice::WriteOnly) || f.write(songToJson(*song)) < 0 || !f.commit()) {
            report.failed << std::pair{song->title, f.errorString()};
            continue;
        }
        report.written << name;
    }
    return report;
}

SongImportReport importSongs(const QString &folder, SongStore &store)
{
    SongImportReport report;
    QSet<QString> titles;
    for (const SongSummary &s : store.list())
        titles.insert(s.title.trimmed().toLower());

    const QFileInfoList files =
        QDir(folder).entryInfoList({QStringLiteral("*") + kSuffix}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        QFile f(file.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            report.failed << std::pair{file.fileName(), f.errorString()};
            continue;
        }
        SongJsonResult r = songFromJson(f.readAll());
        if (!r.song) {
            report.failed << std::pair{file.fileName(), r.error};
            continue;
        }
        Song &song = *r.song;
        if (!song.id.isNull() && store.load(song.id)) {
            report.skipped << std::pair{file.fileName(), QStringLiteral("this song is already in the library")};
            continue;
        }
        if (titles.contains(song.title.toLower()))
            report.warnings << QStringLiteral("%1: a different song titled \"%2\" is already in the library; both are kept")
                                   .arg(file.fileName(), song.title);
        const SongStore::Result saved = store.save(song);
        if (!saved.ok) {
            report.failed << std::pair{file.fileName(), saved.error};
            continue;
        }
        titles.insert(song.title.toLower());
        report.imported << song.title;
    }
    return report;
}

} // namespace visualis
