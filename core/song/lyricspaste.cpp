#include "lyricspaste.h"
#include "labels.h"

#include <QHash>
#include <QRegularExpression>

namespace visualis {

namespace {

struct Header {
    QString label;    // as written, without brackets, colon, or repeat mark: "Chorus"
    int repeat = 1;
};

// "[Chorus x2]", "Chorus (x2):", "(Bridge)", "Verse 1:" -> a header; any lyric line -> nullopt.
std::optional<Header> header(const QString &line)
{
    QString s = line.trimmed();
    if (s.isEmpty() || s.size() > 40)
        return std::nullopt;
    if ((s.startsWith('[') && s.endsWith(']')) || (s.startsWith('(') && s.endsWith(')')))
        s = s.mid(1, s.size() - 2).trimmed();
    if (s.endsWith(':'))
        s.chop(1);

    Header h;
    static const QRegularExpression repeatMark(
        QStringLiteral(R"(^(.*?)\s*[\(\[]?\s*(?:[x×]\s*(\d+)|(\d+)\s*[x×]|(repeat))\s*[\)\]]?\s*:?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = repeatMark.match(s); m.hasMatch() && !m.captured(1).isEmpty()) {
        const QString n = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
        h.repeat = n.isEmpty() ? 1 : qBound(1, n.toInt(), 20);
        s = m.captured(1).trimmed();
        if (s.endsWith(':'))
            s.chop(1);
    }
    if (s.isEmpty() || !parseLabel(s).known)
        return std::nullopt;
    h.label = s;
    return h;
}

struct Footer {
    bool matched = false;
    QString ccliNumber;
    QString copyright;
};

// SongSelect and similar sources end with credit lines that aren't lyrics.
Footer footer(const QString &line)
{
    static const QRegularExpression ccliSong(QStringLiteral(R"(^CCLI\s+Song\s*(?:#|No\.?|Number)?\s*:?\s*(\d+)\s*$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression copyright(QStringLiteral(R"(^(?:©|\(c\)|copyright\b)\s*(.*)$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression other(
        QStringLiteral(R"(^(?:CCLI\s+Licen[cs]e\b.*|For use solely with the SongSelect.*|.*\bTerms of Use\b.*|.*www\.ccli\.com.*|All rights reserved\.?)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QString s = line.trimmed();
    if (auto m = ccliSong.match(s); m.hasMatch())
        return {true, m.captured(1), {}};
    if (auto m = copyright.match(s); m.hasMatch())
        return {true, {}, m.captured(1).trimmed()};
    if (other.match(s).hasMatch())
        return {true, {}, {}};
    return {};
}

// Splits a stanza into slides of at most `max` lines, as evenly as possible (6 lines, max 4 ->
// 3 + 3, not 4 + 2).
QStringList split(const QStringList &lines, int max)
{
    max = qMax(1, max);
    const int slides = (lines.size() + max - 1) / max;
    QStringList out;
    int at = 0;
    for (int i = 0; i < slides; ++i) {
        const int size = (lines.size() - at) / (slides - i) + ((lines.size() - at) % (slides - i) ? 1 : 0);
        out << lines.mid(at, size).join('\n');
        at += size;
    }
    return out;
}

QStringList slidesOf(const QStringList &body, int max)
{
    QStringList slides, stanza;
    for (const QString &line : body) {
        if (line.isEmpty()) {
            if (!stanza.isEmpty())
                slides << split(stanza, max);
            stanza.clear();
        } else {
            stanza << line;
        }
    }
    if (!stanza.isEmpty())
        slides << split(stanza, max);
    return slides;
}

QString normalize(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QChar(0x2028), QChar('\n'));
    text.replace(QChar(0x2029), QChar('\n'));
    text.replace(QChar(0x00A0), QChar(' '));
    text.replace(QChar('\t'), QChar(' '));
    return text;
}

} // namespace

QString defaultGroupColor(const QString &label)
{
    static const QHash<QString, QString> colors = {
        {"verse", "#0078cc"},      {"prechorus", "#cc298c"}, {"chorus", "#cc004f"},  {"postchorus", "#cc5c29"},
        {"bridge", "#7500cc"},     {"refrain", "#cc298c"},   {"tag", "#cc7a00"},     {"vamp", "#cc7a00"},
        {"intro", "#b3a624"},      {"outro", "#998f1f"},     {"ending", "#998f1f"},  {"interlude", "#24b34d"},
        {"instrumental", "#24b34d"}, {"turnaround", "#24b34d"}, {"breakdown", "#24b34d"},
    };
    const SectionLabel l = parseLabel(label);
    return l.known ? colors.value(l.kind) : QString();
}

PastedLyrics parsePastedLyrics(const QString &text, const PasteOptions &options)
{
    PastedLyrics out;
    std::optional<Header> current;      // nullopt: before the first header
    QStringList body;
    bool sawHeader = false;

    auto finish = [&] {
        const QStringList slides = slidesOf(body, options.maxLinesPerSlide);
        body.clear();
        if (!current && slides.isEmpty())
            return;
        const QString label = current ? current->label : QString();
        const int repeat = current ? current->repeat : 1;

        // A label seen before, with no text or the same text, repeats that group.
        for (const Group &g : std::as_const(out.groups)) {
            if (g.label.compare(label, Qt::CaseInsensitive) == 0
                && (slides.isEmpty() || slides == g.slides)) {
                for (int i = 0; i < repeat; ++i)
                    out.sequence << g.id;
                return;
            }
        }
        for (const Group &g : std::as_const(out.groups)) {
            if (!label.isEmpty() && g.label.compare(label, Qt::CaseInsensitive) == 0) {
                out.warnings << QStringLiteral("There are two different \"%1\" sections; both were kept.").arg(label);
                break;
            }
        }
        // A header with no text (Intro, Interlude) gets one blank slide, as in ProPresenter.
        Group g{QUuid::createUuid(), label, defaultGroupColor(label), slides.isEmpty() ? QStringList{QString()} : slides};
        out.groups << g;
        for (int i = 0; i < repeat; ++i)
            out.sequence << g.id;
    };

    bool firstLine = true;
    bool authorsNext = false;   // SongSelect puts the authors on the line after "CCLI Song #"
    for (const QString &raw : normalize(text).split('\n')) {
        const QString line = raw.trimmed();
        if (firstLine && !line.isEmpty()) {
            firstLine = false;
            if (!options.title.isEmpty() && line.compare(options.title.trimmed(), Qt::CaseInsensitive) == 0)
                continue;
        }
        if (const Footer f = footer(line); f.matched) {
            if (!f.ccliNumber.isEmpty())
                out.credits.ccliNumber = f.ccliNumber;
            if (!f.copyright.isEmpty())
                out.credits.copyright = f.copyright;
            authorsNext = !f.ccliNumber.isEmpty();
            continue;
        }
        if (authorsNext && !line.isEmpty()) {
            authorsNext = false;
            out.credits.authors = QString(line).replace(QRegularExpression(QStringLiteral(R"(\s*\|\s*)")), ", ");
            continue;
        }
        if (const std::optional<Header> h = header(line)) {
            finish();
            current = h;
            sawHeader = true;
            continue;
        }
        body << line;
    }
    finish();

    if (!sawHeader && !out.groups.isEmpty())
        out.warnings << QStringLiteral("No section labels found; the lyrics are one unlabelled group.");
    return out;
}

Song songFromPastedLyrics(const QString &title, const QString &text, const PasteOptions &options, QStringList *warnings)
{
    PasteOptions withTitle = options;
    if (withTitle.title.isEmpty())
        withTitle.title = title;
    PastedLyrics p = parsePastedLyrics(text, withTitle);
    Song s;
    s.title = title;
    s.credits = p.credits;
    s.groups = p.groups;

    QList<QUuid> inOrder;
    for (const Group &g : p.groups)
        inOrder << g.id;
    if (p.sequence != inOrder) {
        Arrangement a{QUuid::createUuid(), QStringLiteral("Default"), p.sequence, {}};
        s.arrangements << a;
        s.defaultArrangement = a.id;
    }
    if (warnings)
        *warnings = p.warnings;
    return s;
}

} // namespace visualis
