#include "labels.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace visualis {

namespace {

// Spellings seen in Planning Center and the church's ProPresenter library, folded to one kind.
// Keys have spaces, hyphens, and underscores removed.
const QHash<QString, QString> &spellings()
{
    static const QHash<QString, QString> map = [] {
        // Short forms ("v", "c", "pc", "b") are what Planning Center's short sequences use ("V1 C B").
        const QList<std::pair<QString, QStringList>> kinds = {
            {"prechorus", {"prechorus", "pre", "pc"}},
            {"postchorus", {"postchorus"}},
            {"chorus", {"chorus", "c", "ch"}},
            {"verse", {"verse", "v", "vs"}},
            {"bridge", {"bridge", "b"}},
            {"refrain", {"refrain"}},
            {"tag", {"tag"}},
            {"vamp", {"vamp"}},
            {"intro", {"intro"}},
            {"outro", {"outro"}},
            {"ending", {"ending", "end"}},
            {"interlude", {"interlude"}},
            {"instrumental", {"instrumental", "inst"}},
            {"turnaround", {"turnaround"}},
            {"breakdown", {"breakdown"}},
            {"blank", {"blank"}},
        };
        QHash<QString, QString> m;
        for (const auto &[kind, words] : kinds) {
            for (const QString &w : words)
                m.insert(w, kind);
        }
        return m;
    }();
    return map;
}

const QStringList &languages()
{
    static const QStringList list = {"spanish", "espanol", "tamil", "english", "french", "korean",
                                     "portuguese", "chinese", "mandarin", "swahili", "tagalog"};
    return list;
}

// Lower case, accents removed: "Español" -> "espanol".
QString fold(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    }
    return out.simplified();
}

int editDistance(const QString &a, const QString &b)
{
    QList<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// A known kind for `word`, allowing one typo in words of four or more letters ("refain").
// Shorter words must match exactly, so "tag" never turns into something else, and a typo that
// is one edit from two different kinds matches neither.
QString kindFor(const QString &word)
{
    if (auto it = spellings().constFind(word); it != spellings().constEnd())
        return *it;
    if (word.size() < 4)
        return {};
    QSet<QString> near;
    for (auto it = spellings().constBegin(); it != spellings().constEnd(); ++it) {
        if (it.key().size() >= 4 && editDistance(word, it.key()) <= 1)
            near.insert(*it);
    }
    return near.size() == 1 ? *near.constBegin() : QString();
}

} // namespace

SectionLabel parseLabel(const QString &text)
{
    QString s = fold(text);
    SectionLabel label;

    // "Spanish - Chorus 1" or "Bridge 1 Tamil"
    for (const QString &lang : languages()) {
        static const QString sep = QStringLiteral("\\s*[-:–]\\s*");
        QRegularExpression before("^" + lang + sep + "(.*)$"), after("^(.*?)\\s+" + lang + "$");
        if (auto m = before.match(s); m.hasMatch()) {
            label.language = lang == "espanol" ? "spanish" : lang;
            s = m.captured(1);
            break;
        }
        if (auto m = after.match(s); m.hasMatch()) {
            label.language = lang == "espanol" ? "spanish" : lang;
            s = m.captured(1);
            break;
        }
    }

    static const QRegularExpression trailingNumber(QStringLiteral("^(.*?)\\s*(\\d+)$"));
    if (auto m = trailingNumber.match(s); m.hasMatch() && !m.captured(1).isEmpty()) {
        s = m.captured(1);
        label.number = m.captured(2).toInt();
    }
    static const QRegularExpression separators(QStringLiteral("[\\s\\-_.]+"));
    const QString word = QString(s).remove(separators);
    label.kind = kindFor(word);
    label.known = !label.kind.isEmpty();
    if (!label.known)
        label.kind = word;
    return label;
}

SequenceMatch matchSequence(const QStringList &labels, const QList<Group> &groups, const QString &language)
{
    struct Candidate {
        QUuid id;
        SectionLabel label;
    };
    QList<Candidate> candidates;
    for (const Group &g : groups) {
        SectionLabel l = parseLabel(g.label);
        if (l.language == language)
            candidates << Candidate{g.id, l};
    }

    SequenceMatch result;
    for (const QString &text : labels) {
        const SectionLabel want = parseLabel(text);
        QList<Candidate> sameKind;
        for (const Candidate &c : candidates) {
            if (c.label.kind == want.kind)
                sameKind << c;
        }
        auto withNumber = [&](std::optional<int> n) {
            QList<QUuid> ids;
            for (const Candidate &c : sameKind) {
                if (c.label.number == n)
                    ids << c.id;
            }
            return ids;
        };

        QList<QUuid> hits = withNumber(want.number);
        if (hits.isEmpty() && sameKind.size() == 1 && (!sameKind[0].label.number || !want.number))
            hits = {sameKind[0].id};
        if (hits.isEmpty() && want.number == 1)
            hits = withNumber(std::nullopt);
        if (hits.isEmpty() && !want.number)
            hits = withNumber(1);

        const QUuid hit = hits.size() == 1 ? hits[0] : QUuid();
        result.steps << hit;
        if (hit.isNull() && !result.unmatched.contains(text))
            result.unmatched << text;
    }
    return result;
}

} // namespace visualis
