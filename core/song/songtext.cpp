#include "song/songtext.h"

#include "song/lyricspaste.h"

#include <algorithm>

namespace visualis {

namespace {

// One group as the editor shows it.
QString groupText(const Group &g)
{
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
    return lines.join('\n');
}

PasteOptions editorOptions()
{
    PasteOptions options;
    options.maxLinesPerSlide = 1000;   // the editor's slide breaks are the operator's own
    options.editorText = true;
    return options;
}

// The slides `g` reads back as from its own editor text. The parser trims lines and
// normalizes spaces, so this, not g.slides, is what an untouched group parses to.
QStringList slidesAsEdited(const Group &g)
{
    const PastedLyrics parsed = parsePastedLyrics(groupText(g), editorOptions());
    return parsed.groups.size() == 1 ? parsed.groups[0].slides : QStringList();
}

// Gives `edited`, the group `was` after a text edit, the slide overrides of `was`, so each
// follows its slide (see applyLyricsText). `wasText` is was's slides as the parser reads
// them, to compare with edited's. Returns how many non-empty overrides found no slide.
int remapSlideOverrides(const Group &was, QStringList wasText, Group &edited)
{
    if (wasText.size() != was.slides.size())
        wasText = was.slides;   // can't line them up as parsed; compare as stored
    const qsizetype count = edited.slides.size();
    edited.slideOverrides = QList<QJsonObject>(count);
    QList<bool> taken(wasText.size(), false);
    QList<bool> placed(count, false);
    // The same text, in order, so a reordered or repeated slide keeps its own.
    for (qsizetype n = 0; n < count; ++n) {
        for (qsizetype o = 0; o < wasText.size(); ++o) {
            if (!taken[o] && wasText[o] == edited.slides[n]) {
                edited.slideOverrides[n] = was.slideOverride(o);
                taken[o] = placed[n] = true;
                break;
            }
        }
    }
    // A typo fix: with as many slides as before, the rest match by position.
    if (count == wasText.size()) {
        for (qsizetype n = 0; n < count; ++n) {
            if (!placed[n] && !taken[n]) {
                edited.slideOverrides[n] = was.slideOverride(n);
                taken[n] = true;
            }
        }
    }
    int lost = 0;
    for (qsizetype o = 0; o < wasText.size(); ++o) {
        if (!taken[o] && !was.slideOverride(o).isEmpty())
            ++lost;
    }
    return lost;
}

} // namespace

QString lyricsText(const Song &song)
{
    QStringList blocks;
    for (const Group &g : song.groups)
        blocks << groupText(g);
    return blocks.join(QStringLiteral("\n\n"));
}

LyricsEdit applyLyricsText(const Song &song, const QString &text)
{
    const PastedLyrics parsed = parsePastedLyrics(text, editorOptions());

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
            const Group &was = old[matchOf[p]];
            // The text owns only the label and slides; everything else stays the group's.
            g = was;
            g.label = parsed.groups[p].label;
            g.slides = parsed.groups[p].slides;
            // A group whose text wasn't touched keeps its slides exactly as they were (the parser
            // would trim them).
            // Remapped first, so parsed text is compared with parsed text.
            const QStringList wasEdited = slidesAsEdited(was);
            const int lost = remapSlideOverrides(was, wasEdited, g);
            if (g.slides == wasEdited)
                g.slides = was.slides;
            if (lost > 0)
                out.warnings << QStringLiteral("\"%1\"'s slides changed, so the style changes of %2 %3 were dropped.")
                                    .arg(g.label).arg(lost).arg(lost == 1 ? QStringLiteral("slide") : QStringLiteral("slides"));
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
        if (const auto styled = std::count_if(old[o].slideOverrides.cbegin(), old[o].slideOverrides.cend(),
                                              [](const QJsonObject &so) { return !so.isEmpty(); }))
            out.warnings << QStringLiteral("\"%1\" was removed, and with it the style changes of %2 %3.")
                                .arg(old[o].label).arg(styled).arg(styled == 1 ? QStringLiteral("slide") : QStringLiteral("slides"));
    }
    return out;
}

} // namespace visualis
