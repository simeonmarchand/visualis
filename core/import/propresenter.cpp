#include "propresenter.h"
#include "protobuf.h"
#include "rtf.h"
#include "song/labels.h"
#include "song/songstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <cmath>

namespace visualis::propresenter {

namespace {

// Field numbers from github.com/greyshirtguy/ProPresenter7-Proto (proto/*.proto).
namespace F {
constexpr int Presentation_name = 3, Presentation_selectedArrangement = 10, Presentation_arrangements = 11,
              Presentation_cueGroups = 12, Presentation_cues = 13, Presentation_ccli = 14;
constexpr int Arrangement_uuid = 1, Arrangement_name = 2, Arrangement_groupIdentifiers = 3;
constexpr int CueGroup_group = 1, CueGroup_cueIdentifiers = 2;
constexpr int Group_uuid = 1, Group_name = 2, Group_color = 3;
constexpr int Color_red = 1, Color_green = 2, Color_blue = 3, Color_alpha = 4;
constexpr int Cue_uuid = 1, Cue_actions = 10;
constexpr int Action_slide = 23, SlideType_presentation = 2, PresentationSlide_baseSlide = 1;
constexpr int Slide_elements = 1, SlideElement_element = 1, GraphicsElement_text = 13, Text_rtfData = 5;
constexpr int Ccli_author = 1, Ccli_artist = 2, Ccli_title = 3, Ccli_publisher = 4, Ccli_year = 5, Ccli_songNumber = 6;
constexpr int Uuid_string = 1;
} // namespace F

QString uuidOf(const ProtoMessage &m, int field)
{
    return m.message(field).string(F::Uuid_string);
}

// Colors are 0..1 floats; a fully transparent color means "no color".
QString colorOf(const ProtoMessage &group)
{
    const ProtoMessage c = group.message(F::Group_color);
    if (c.real(F::Color_alpha).value_or(0) <= 0)
        return {};
    auto channel = [&](int f) { return qBound(0, int(std::lround(c.real(f).value_or(0) * 255)), 255); };
    return QStringLiteral("#%1%2%3")
        .arg(channel(F::Color_red), 2, 16, QChar('0'))
        .arg(channel(F::Color_green), 2, 16, QChar('0'))
        .arg(channel(F::Color_blue), 2, 16, QChar('0'));
}

// All text on a slide, element by element (cue -> actions -> slide -> elements -> text).
QString slideText(const ProtoMessage &cue)
{
    QStringList texts;
    for (const ProtoMessage &action : cue.messages(F::Cue_actions)) {
        const ProtoMessage slide = action.message(F::Action_slide).message(F::SlideType_presentation)
                                       .message(F::PresentationSlide_baseSlide);
        for (const ProtoMessage &element : slide.messages(F::Slide_elements)) {
            const QByteArray rtf = element.message(F::SlideElement_element).message(F::GraphicsElement_text)
                                       .bytes(F::Text_rtfData);
            if (rtf.isEmpty())
                continue;
            const QString t = rtfToText(rtf);
            if (!t.trimmed().isEmpty())
                texts << t;
        }
    }
    return texts.join('\n');
}

} // namespace

ReadResult readBytes(const QByteArray &bytes, const QString &title)
{
    ReadResult r;
    const std::optional<ProtoMessage> parsed = ProtoMessage::parse(bytes);
    if (!parsed || bytes.isEmpty()) {
        r.error = QStringLiteral("not a ProPresenter 7 file, or it's damaged");
        return r;
    }
    const ProtoMessage &p = *parsed;

    QHash<QString, QString> cueText;   // cue uuid -> text
    for (const ProtoMessage &cue : p.messages(F::Presentation_cues))
        cueText.insert(uuidOf(cue, F::Cue_uuid), slideText(cue));

    Song song;
    song.title = title.trimmed();
    QHash<QString, QUuid> groupIds;    // ProPresenter group uuid -> new id
    QSet<QString> grouped;
    int missingCues = 0;
    for (const ProtoMessage &cg : p.messages(F::Presentation_cueGroups)) {
        const ProtoMessage g = cg.message(F::CueGroup_group);
        Group group{QUuid::createUuid(), g.string(F::Group_name).trimmed(), colorOf(g), {}};
        for (const QByteArray &idBytes : cg.allBytes(F::CueGroup_cueIdentifiers)) {
            const QString cueId = ProtoMessage::parse(idBytes).value_or(ProtoMessage()).string(F::Uuid_string);
            grouped.insert(cueId);
            if (auto it = cueText.constFind(cueId); it != cueText.constEnd())
                group.slides << *it;
            else
                ++missingCues;
        }
        groupIds.insert(uuidOf(g, F::Group_uuid), group.id);
        song.groups << group;
    }
    if (missingCues)
        r.warnings << QStringLiteral("%1 slide(s) referenced by groups weren't in the file").arg(missingCues);

    // Slides that aren't in any group still belong to the presentation.
    QStringList loose;
    for (const ProtoMessage &cue : p.messages(F::Presentation_cues)) {
        if (!grouped.contains(uuidOf(cue, F::Cue_uuid)))
            loose << cueText.value(uuidOf(cue, F::Cue_uuid));
    }
    if (!loose.isEmpty()) {
        song.groups << Group{QUuid::createUuid(), QString(), QString(), loose};
        r.warnings << QStringLiteral("%1 slide(s) weren't in any group; they were put in an unlabelled group")
                          .arg(loose.size());
    }

    const QString selected = uuidOf(p, F::Presentation_selectedArrangement);
    int unknownSteps = 0;
    for (const ProtoMessage &a : p.messages(F::Presentation_arrangements)) {
        Arrangement arr{QUuid::createUuid(), a.string(F::Arrangement_name).trimmed(), {}, {}};
        for (const QByteArray &idBytes : a.allBytes(F::Arrangement_groupIdentifiers)) {
            const QString gid = ProtoMessage::parse(idBytes).value_or(ProtoMessage()).string(F::Uuid_string);
            if (auto it = groupIds.constFind(gid); it != groupIds.constEnd())
                arr.sequence << *it;
            else
                ++unknownSteps;
        }
        if (arr.name.isEmpty())
            arr.name = QStringLiteral("Arrangement %1").arg(song.arrangements.size() + 1);
        if (!selected.isEmpty() && uuidOf(a, F::Arrangement_uuid) == selected)
            song.defaultArrangement = arr.id;
        song.arrangements << arr;
    }
    if (unknownSteps)
        r.warnings << QStringLiteral("%1 arrangement step(s) pointed at groups that don't exist; they were left out")
                          .arg(unknownSteps);

    const ProtoMessage ccli = p.message(F::Presentation_ccli);
    if (const quint64 number = ccli.varint(F::Ccli_songNumber))
        song.credits.ccliNumber = QString::number(number);
    QString authors = ccli.string(F::Ccli_author).trimmed();
    if (authors.isEmpty())
        authors = ccli.string(F::Ccli_artist).trimmed();
    song.credits.authors = authors;
    const QString publisher = ccli.string(F::Ccli_publisher).trimmed();
    if (!publisher.isEmpty()) {
        const quint64 year = ccli.varint(F::Ccli_year);
        song.credits.copyright = year ? QStringLiteral("%1 %2").arg(year).arg(publisher) : publisher;
    }

    for (const Group &g : std::as_const(song.groups)) {
        if (parseLabel(g.label).known) {
            r.looksLikeSong = true;
            break;
        }
    }
    r.song = song;
    return r;
}

ReadResult readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ReadResult r;
        r.error = f.errorString();
        return r;
    }
    // Bounded, in case the file grew since it was listed.
    const QByteArray bytes = f.read(kMaxFileBytes + 1);
    if (bytes.size() > kMaxFileBytes) {
        ReadResult r;
        r.error = QStringLiteral("too large to be a song");
        return r;
    }
    return readBytes(bytes, QFileInfo(path).completeBaseName());
}

QFileInfoList presentationFiles(const QString &folder)
{
    return QDir(folder).entryInfoList({"*.pro"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
}

ImportFile readForImport(const QFileInfo &file)
{
    ImportFile out{file.fileName(), {}, {}, {}, {}};
    if (file.size() > kMaxFileBytes) {
        out.skipped = QStringLiteral("too large to be a song");
        return out;
    }
    ReadResult r = readFile(file.absoluteFilePath());
    if (!r.song) {
        out.error = r.error;
        return out;
    }
    if (!r.looksLikeSong) {
        out.skipped = QStringLiteral("not a song (no Verse, Chorus, or similar groups)");
        return out;
    }
    out.song = std::move(r.song);
    out.warnings = r.warnings;
    return out;
}

ImportReport importFolder(const QString &folder, SongStore &store, const ImportProgress &progress)
{
    ImportWriter writer(store, ImportWriter::Duplicates::SkipSameTitle);
    const QFileInfoList files = presentationFiles(folder);
    const int total = int(files.size());
    for (int i = 0; i < total; ++i) {
        if (progress && !progress(i, total))
            return writer.report();
        writer.add(readForImport(files[i]));
    }
    if (progress)
        progress(total, total);
    return writer.report();
}

} // namespace visualis::propresenter
