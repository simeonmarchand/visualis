#include "song/songmatch.h"

#include <QRegularExpression>
#include <algorithm>

namespace visualis {

namespace {

QString digits(const QString &s)
{
    QString out;
    for (QChar c : s) {
        if (c.isDigit())
            out += c;
    }
    return out;
}

QString folded(const QString &s)
{
    QString out;
    for (QChar c : s.normalized(QString::NormalizationForm_KD)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    }
    return out;
}

// Plain edit distance, for titles short enough that the quadratic cost doesn't matter.
int distance(const QString &a, const QString &b)
{
    QList<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = up;
        }
    }
    return row[b.size()];
}

// How alike two normalized titles are, or -1 when they aren't: a typo or two
// ("Amazng Grace"), or one title the start of the other ("Amazing Grace" and "Amazing
// Grace My Chains Are Gone"). Lower is closer.
int similarity(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return -1;
    const int allowed = std::min(a.size(), b.size()) >= 8 ? 2 : std::min(a.size(), b.size()) >= 4 ? 1 : 0;
    if (const int d = distance(a, b); d <= allowed)
        return d;
    const QString &shorter = a.size() < b.size() ? a : b;
    const QString &longer = a.size() < b.size() ? b : a;
    if (shorter.count(QLatin1Char(' ')) >= 1 && longer.startsWith(shorter + QLatin1Char(' ')))
        return 10 + int(longer.size() - shorter.size());
    return -1;
}

bool sharesAuthor(const QString &pcoAuthor, const QString &authors)
{
    static const QRegularExpression separators(QStringLiteral("[,;&/]|\\band\\b"));
    const QString theirs = folded(authors);
    for (const QString &name : folded(pcoAuthor).split(separators, Qt::SkipEmptyParts)) {
        if (name.trimmed().size() >= 3 && theirs.contains(name.trimmed()))
            return true;
    }
    return false;
}

SongMatch confirm(SongMatch::By by, QList<QUuid> ids)
{
    if (ids.size() > 3)
        ids.resize(3);
    return SongMatch{SongMatch::Kind::Confirm, by, {}, ids};
}

} // namespace

// What's left of a title with every bracketed note removed, for suggesting "Psalm 23 (I Am
// Not Alone)" when the library has "Psalm 23 (Surely Goodness)": never for linking.
static QString withoutNotes(const QString &title)
{
    static const QRegularExpression note(QStringLiteral("\\s*[(\\[][^)\\]]*[)\\]]"));
    const QString stripped = QString(title).remove(note);
    return normalizedTitle(stripped.trimmed().isEmpty() ? title : stripped);
}

QString normalizedTitle(const QString &title)
{
    // Only notes about a recording. Others name the song ("Psalm 23 (I Am Not Alone)").
    static const QRegularExpression note(
        QStringLiteral("\\s*[(\\[]\\s*(?:live(?:\\s+[^)\\]]*)?|acoustic|studio(?:\\s+version)?|radio\\s+(?:version|edit)"
                       "|(?:feat|ft)\\.?\\s[^)\\]]*|official[^)\\]]*|remaster(?:ed)?[^)\\]]*)\\s*[)\\]]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression apostrophe(QStringLiteral("['\\x{2019}\\x{2018}`]"));
    static const QRegularExpression other(QStringLiteral("[^\\p{L}\\p{N}]+"), QRegularExpression::UseUnicodePropertiesOption);
    QString s = folded(title);
    while (s.contains(note) && !QString(s).remove(note).trimmed().isEmpty())
        s.remove(note);
    s.replace(QLatin1Char('&'), QStringLiteral(" and "));
    s.remove(apostrophe);
    s = s.replace(other, QStringLiteral(" ")).trimmed();
    if (s.startsWith(QStringLiteral("the ")))
        s = s.mid(4);
    return s;
}

SongMatch matchSong(const PcoSong &song, const QList<SongCandidate> &library)
{
    // 1. Linked before.
    if (!song.id.isEmpty()) {
        QList<QUuid> linked;
        for (const SongCandidate &c : library) {
            if (c.pcoSongId == song.id)
                linked << c.id;
        }
        if (linked.size() == 1)
            return {SongMatch::Kind::Linked, SongMatch::By::PcoSongId, linked.first(), {}};
        if (linked.size() > 1)
            return confirm(SongMatch::By::PcoSongId, linked);
    }

    const QString title = normalizedTitle(song.title);
    const QString ccli = digits(song.ccliNumber);
    // A library song that is surely some other song, whatever its title.
    const auto other = [&](const SongCandidate &c) {
        return (!c.pcoSongId.isEmpty() && !song.id.isEmpty() && c.pcoSongId != song.id)
               || (!digits(c.ccliNumber).isEmpty() && !ccli.isEmpty() && digits(c.ccliNumber) != ccli);
    };

    // 2. CCLI number.
    if (!ccli.isEmpty()) {
        QList<QUuid> same;
        QList<QUuid> sameAndTitled;
        for (const SongCandidate &c : library) {
            if (digits(c.ccliNumber) == ccli) {
                same << c.id;
                if (normalizedTitle(c.title) == title)
                    sameAndTitled << c.id;
            }
        }
        if (same.size() == 1)
            return {SongMatch::Kind::Linked, SongMatch::By::Ccli, same.first(), {}};
        if (sameAndTitled.size() == 1)
            return {SongMatch::Kind::Linked, SongMatch::By::Ccli, sameAndTitled.first(), {}};
        if (same.size() > 1)
            return confirm(SongMatch::By::Ccli, same);
    }

    // 3 and 4. Titles.
    if (title.isEmpty())
        return {};
    QList<const SongCandidate *> equal;
    struct Near { const SongCandidate *c; int score; };
    QList<Near> near;
    for (const SongCandidate &c : library) {
        const QString theirs = normalizedTitle(c.title);
        if (theirs == title)
            equal << &c;
        else if (const int s = similarity(title, theirs); s >= 0)
            near << Near{&c, s};
        else if (withoutNotes(c.title) == withoutNotes(song.title))
            near << Near{&c, 5};   // the same but for a note in brackets: ask
    }
    const auto byAuthor = [&](QList<const SongCandidate *> list) {
        std::stable_partition(list.begin(), list.end(),
                              [&](const SongCandidate *c) { return sharesAuthor(song.author, c->authors); });
        QList<QUuid> ids;
        for (const SongCandidate *c : list)
            ids << c->id;
        return ids;
    };
    if (equal.size() == 1 && !other(*equal.first()))
        return {SongMatch::Kind::Linked, SongMatch::By::Title, equal.first()->id, {}};
    if (!equal.isEmpty())
        return confirm(SongMatch::By::Title, byAuthor(equal));
    if (!near.isEmpty()) {
        std::stable_sort(near.begin(), near.end(), [](const Near &a, const Near &b) { return a.score < b.score; });
        QList<const SongCandidate *> list;
        for (const Near &n : near)
            list << n.c;
        return confirm(SongMatch::By::SimilarTitle, byAuthor(list));
    }
    return {};
}

std::optional<PcoSong> snapshotSong(const QJsonObject &importSnapshot)
{
    if (!importSnapshot.contains(QStringLiteral("song")))
        return std::nullopt;
    const QJsonObject s = importSnapshot.value(QStringLiteral("song")).toObject();
    return PcoSong{s.value("id").toString(), s.value("title").toString(), s.value("ccliNumber").toString(),
                   s.value("author").toString(), s.value("copyright").toString()};
}

} // namespace visualis
