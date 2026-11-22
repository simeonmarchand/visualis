#include "song/songtext.h"

#include "song/lyricspaste.h"

namespace visualis {

QString lyricsText(const Song &song)
{
    QStringList blocks;
    for (const Group &g : song.groups) {
        QStringList lines{QStringLiteral("[%1]").arg(g.label.isEmpty() ? QStringLiteral("Untitled") : g.label)};
        QStringList slides;
        for (const QString &s : g.slides) {
            if (!s.trimmed().isEmpty())
                slides << s;
        }
        if (!slides.isEmpty())
            lines << slides.join(QStringLiteral("\n\n"));
        blocks << lines.join('\n');
    }
    return blocks.join(QStringLiteral("\n\n"));
}

LyricsEdit applyLyricsText(const Song &song, const QString &text)
{
    PasteOptions options;
    options.maxLinesPerSlide = 1000;   // the editor's slide breaks are the operator's own
    const PastedLyrics parsed = parsePastedLyrics(text, options);

    const QList<Group> &old = song.groups;
    QList<int> matchOf(parsed.groups.size(), -1);   // parsed group -> old group index
    QList<bool> used(old.size(), false);

    // Same label, ignoring case.
    for (int p = 0; p < parsed.groups.size(); ++p) {
        for (int o = 0; o < old.size(); ++o) {
            if (!used[o] && old[o].label.compare(parsed.groups[p].label, Qt::CaseInsensitive) == 0) {
                matchOf[p] = o;
                used[o] = true;
                break;
            }
        }
    }
    // Renamed in place: an unmatched header where an unmatched group was.
    for (int p = 0; p < parsed.groups.size(); ++p) {
        if (matchOf[p] < 0 && p < old.size() && !used[p]) {
            matchOf[p] = p;
            used[p] = true;
        }
    }

    LyricsEdit out;
    out.song = song;
    out.song.groups.clear();
    out.warnings = parsed.warnings;
    for (int p = 0; p < parsed.groups.size(); ++p) {
        Group g = parsed.groups[p];
        if (matchOf[p] >= 0) {
            g.id = old[matchOf[p]].id;
            g.color = old[matchOf[p]].color;
        } else if (!song.arrangements.isEmpty()) {
            out.warnings << QStringLiteral("\"%1\" is new and isn't in any arrangement yet.").arg(g.label);
        }
        out.song.groups.append(g);
    }
    for (int o = 0; o < old.size(); ++o) {
        if (used[o])
            continue;
        const bool arranged = std::any_of(song.arrangements.cbegin(), song.arrangements.cend(),
                                          [&](const Arrangement &a) { return a.sequence.contains(old[o].id); });
        for (Arrangement &a : out.song.arrangements)
            a.sequence.removeAll(old[o].id);
        if (arranged)
            out.warnings << QStringLiteral("\"%1\" was removed, and with it its places in the arrangements.").arg(old[o].label);
    }
    return out;
}

} // namespace visualis
