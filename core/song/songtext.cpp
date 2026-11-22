#include "song/songtext.h"

#include "song/lyricspaste.h"

#include <algorithm>

namespace visualis {

QString lyricsText(const Song &song)
{
    QStringList blocks;
    for (const Group &g : song.groups) {
        QStringList lines{QStringLiteral("[%1]").arg(g.label)};
        const bool onlyBlank = std::all_of(g.slides.cbegin(), g.slides.cend(),
                                           [](const QString &s) { return s.trimmed().isEmpty(); });
        // One blank slide is how a header with no lyrics reads back (an Intro).
        if (!onlyBlank || g.slides.size() > 1) {
            QStringList slides;
            for (const QString &s : g.slides) {
                QStringList slideLines = s.split('\n');
                for (QString &l : slideLines)
                    l = escapeEditorLine(l);
                slides << (s.trimmed().isEmpty() ? blankSlideLine() : slideLines.join('\n'));
            }
            lines << slides.join(QStringLiteral("\n\n"));
        }
        blocks << lines.join('\n');
    }
    return blocks.join(QStringLiteral("\n\n"));
}

LyricsEdit applyLyricsText(const Song &song, const QString &text)
{
    PasteOptions options;
    options.maxLinesPerSlide = 1000;   // the editor's slide breaks are the operator's own
    options.editorText = true;
    const PastedLyrics parsed = parsePastedLyrics(text, options);

    const QList<Group> &old = song.groups;
    QList<int> matchOf(parsed.groups.size(), -1);   // parsed group -> old group index
    QList<bool> used(old.size(), false);

    // Same label, ignoring case. With duplicates (two Choruses) the first unused one isn't
    // necessarily the same group, so try the same position, then the same lyrics, first.
    const auto sameLabel = [&](int p, int o) {
        return old[o].label.compare(parsed.groups[p].label, Qt::CaseInsensitive) == 0;
    };
    const auto matchWhere = [&](auto fits) {
        for (int p = 0; p < parsed.groups.size(); ++p) {
            for (int o = 0; matchOf[p] < 0 && o < old.size(); ++o) {
                if (!used[o] && sameLabel(p, o) && fits(p, o)) {
                    matchOf[p] = o;
                    used[o] = true;
                }
            }
        }
    };
    matchWhere([](int p, int o) { return p == o; });
    matchWhere([&](int p, int o) { return parsed.groups[p].slides == old[o].slides; });
    matchWhere([](int, int) { return true; });
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
