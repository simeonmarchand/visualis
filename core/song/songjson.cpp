#include "songjson.h"
#include "songlimits.h"
#include "songstore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace visualis {

namespace {

constexpr int kVersion = 1;
const char *kFormat = "visualis-song";
const char *kSuffix = ".visualis-song.json";
// File systems limit a name to 255 bytes (APFS and ext4 count UTF-8 bytes), so the stem is
// budgeted in UTF-8 bytes, with room left for a collision tag " (n)", the suffix, and
// QSaveFile's temporary ".XXXXXX": 120 + 16 + 19 + 7 bytes stays well under 255.
constexpr qsizetype kMaxStemBytes = 120;
constexpr qsizetype kMaxTagBytes = 16;

// The longest prefix of `s` that fits in `maxBytes` of UTF-8, never splitting a character.
QString leftUtf8(const QString &s, qsizetype maxBytes)
{
    qsizetype bytes = 0;
    qsizetype i = 0;
    while (i < s.size()) {
        const bool pair = s[i].isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate();
        const char32_t cp = pair ? QChar::surrogateToUcs4(s[i], s[i + 1]) : s[i].unicode();
        // A lone surrogate is written as U+FFFD: 3 bytes.
        const int n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (bytes + n > maxBytes)
            break;
        bytes += n;
        i += pair ? 2 : 1;
    }
    return s.left(i);
}

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
        QJsonObject group{{"id", idText(g.id)}, {"label", g.label}, {"color", g.color},
                          {"slides", QJsonArray::fromStringList(g.slides)}};
        // Only when there are some, so files without overrides read as they always have.
        if (std::any_of(g.slideOverrides.cbegin(), g.slideOverrides.cend(), [](const QJsonObject &o) { return !o.isEmpty(); })) {
            QJsonArray overrides;
            for (const QJsonObject &o : g.slideOverrides)
                overrides.append(o);
            group["slideOverrides"] = overrides;
        }
        groups.append(group);
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
    if (!s.defaultTemplate.isNull())
        o["defaultTemplate"] = idText(s.defaultTemplate);
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
    s.defaultTemplate = QUuid::fromString(o.value("defaultTemplate").toString());
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
        Group group{id, g.value("label").toString(), g.value("color").toString(), slides};
        // Parallel to the slides; extras would belong to no slide.
        for (const QJsonValue &o : g.value("slideOverrides").toArray()) {
            if (group.slideOverrides.size() == group.slides.size())
                break;
            group.slideOverrides << o.toObject();
        }
        s.groups << group;
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
    return leftUtf8(name, kMaxStemBytes) + kSuffix;
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
        // The stem's budget leaves room for " (n)", so the tag is never cut off.
        const QString stem = name.chopped(qsizetype(qstrlen(kSuffix)));
        for (int n = 2; used.contains(name.toLower()); ++n) {
            const QString tag = QStringLiteral(" (%1)").arg(n);
            Q_ASSERT(tag.size() <= kMaxTagBytes);
            name = stem + tag + kSuffix;
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

QFileInfoList songFiles(const QString &folder)
{
    return QDir(folder).entryInfoList({QStringLiteral("*") + kSuffix}, QDir::Files, QDir::Name | QDir::IgnoreCase);
}

ImportFile readSongFile(const QFileInfo &file)
{
    ImportFile out{file.fileName(), {}, {}, {}, {}};
    if (file.size() > kMaxSongFileBytes) {
        out.skipped = QStringLiteral("too large to be a song");
        return out;
    }
    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        out.error = f.errorString();
        return out;
    }
    const QByteArray bytes = f.read(kMaxSongFileBytes + 1);   // bounded, in case it grew since listed
    if (bytes.size() > kMaxSongFileBytes) {
        out.skipped = QStringLiteral("too large to be a song");
        return out;
    }
    SongJsonResult r = songFromJson(bytes);
    if (!r.song) {
        out.error = r.error;
        return out;
    }
    // Checked here, on the worker, so an oversized song never reaches the library's writer (#83).
    if (const QString tooBig = songLimitError(*r.song); !tooBig.isEmpty()) {
        out.skipped = tooBig;
        return out;
    }
    out.song = std::move(r.song);
    return out;
}

SongImportReport importSongs(const QString &folder, SongStore &store, const ImportProgress &progress)
{
    ImportWriter writer(store, ImportWriter::Duplicates::SkipSameIdWarnSameTitle);
    const QFileInfoList files = songFiles(folder);
    const int total = int(files.size());
    for (int i = 0; i < total; ++i) {
        if (progress && !progress(i, total))
            return writer.report();
        writer.add(readSongFile(files[i]));
    }
    if (progress)
        progress(total, total);
    return writer.report();
}

} // namespace visualis
