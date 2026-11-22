#include "scripture/reference.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

namespace visualis {

namespace {

struct Book {
    const char *name;
    int chapters;
    QStringList aliases;   // lowercase, as people type them; the full name is added below
};

// The 66-book canon and common abbreviations, from spikes/scripture/books.py.
const QList<Book> &books()
{
    static const QList<Book> all{
        {"Genesis", 50, {"gen", "gn", "ge"}},
        {"Exodus", 40, {"exo", "ex", "exod"}},
        {"Leviticus", 27, {"lev", "lv", "le"}},
        {"Numbers", 36, {"num", "nm", "nu"}},
        {"Deuteronomy", 34, {"deu", "dt", "deut"}},
        {"Joshua", 24, {"jos", "josh", "jsh"}},
        {"Judges", 21, {"jdg", "judg", "jg"}},
        {"Ruth", 4, {"rut", "ru", "rth"}},
        {"1 Samuel", 31, {"1 sa", "1 sam", "1 sm"}},
        {"2 Samuel", 24, {"2 sa", "2 sam", "2 sm"}},
        {"1 Kings", 22, {"1 ki", "1 kgs", "1 kin"}},
        {"2 Kings", 25, {"2 ki", "2 kgs", "2 kin"}},
        {"1 Chronicles", 29, {"1 ch", "1 chr", "1 chron"}},
        {"2 Chronicles", 36, {"2 ch", "2 chr", "2 chron"}},
        {"Ezra", 10, {"ezr"}},
        {"Nehemiah", 13, {"neh", "ne"}},
        {"Esther", 10, {"est", "esth", "es"}},
        {"Job", 42, {"job", "jb"}},
        {"Psalms", 150, {"psa", "psalm", "ps", "pss", "psm"}},
        {"Proverbs", 31, {"pro", "prov", "prv", "pr"}},
        {"Ecclesiastes", 12, {"ecc", "eccl", "eccles", "qoh"}},
        {"Song of Solomon", 8, {"sng", "song", "song of songs", "sos", "so", "canticles", "song of sol"}},
        {"Isaiah", 66, {"isa", "is"}},
        {"Jeremiah", 52, {"jer", "jr"}},
        {"Lamentations", 5, {"lam", "la"}},
        {"Ezekiel", 48, {"ezk", "ezek", "eze"}},
        {"Daniel", 12, {"dan", "dn", "da"}},
        {"Hosea", 14, {"hos", "ho"}},
        {"Joel", 3, {"jol", "jl"}},
        {"Amos", 9, {"amo", "am"}},
        {"Obadiah", 1, {"oba", "obad", "ob"}},
        {"Jonah", 4, {"jon", "jnh"}},
        {"Micah", 7, {"mic", "mi"}},
        {"Nahum", 3, {"nam", "nah", "na"}},
        {"Habakkuk", 3, {"hab", "hb"}},
        {"Zephaniah", 3, {"zep", "zeph", "zp"}},
        {"Haggai", 2, {"hag", "hg"}},
        {"Zechariah", 14, {"zec", "zech", "zc"}},
        {"Malachi", 4, {"mal", "ml"}},
        {"Matthew", 28, {"mat", "matt", "mt"}},
        {"Mark", 16, {"mrk", "mk", "mr"}},
        {"Luke", 24, {"luk", "lk", "lu"}},
        {"John", 21, {"jhn", "jn", "joh"}},
        {"Acts", 28, {"act", "ac", "acts of the apostles"}},
        {"Romans", 16, {"rom", "ro", "rm"}},
        {"1 Corinthians", 16, {"1 co", "1 cor"}},
        {"2 Corinthians", 13, {"2 co", "2 cor"}},
        {"Galatians", 6, {"gal", "ga"}},
        {"Ephesians", 6, {"eph", "ephes"}},
        {"Philippians", 4, {"php", "phil"}},
        {"Colossians", 4, {"col"}},
        {"1 Thessalonians", 5, {"1 th", "1 thess", "1 thes"}},
        {"2 Thessalonians", 3, {"2 th", "2 thess", "2 thes"}},
        {"1 Timothy", 6, {"1 ti", "1 tim", "1 tm"}},
        {"2 Timothy", 4, {"2 ti", "2 tim", "2 tm"}},
        {"Titus", 3, {"tit", "ti"}},
        {"Philemon", 1, {"phm", "philem", "pm"}},
        {"Hebrews", 13, {"heb"}},
        {"James", 5, {"jas", "jm"}},
        {"1 Peter", 5, {"1 pe", "1 pet", "1 pt"}},
        {"2 Peter", 3, {"2 pe", "2 pet", "2 pt"}},
        {"1 John", 5, {"1 jn", "1 jo", "1 jhn"}},
        {"2 John", 1, {"2 jn", "2 jo", "2 jhn"}},
        {"3 John", 1, {"3 jn", "3 jo", "3 jhn"}},
        {"Jude", 1, {"jud", "jd"}},
        {"Revelation", 22, {"rev", "re", "rv", "revelations", "apocalypse"}},
    };
    return all;
}

// "I Cor." -> "1 cor", "1Co" -> "1 co", "First  John" -> "1 john".
QString fold(const QString &name)
{
    static const QRegularExpression space(QStringLiteral("[.\\s]+"));
    static const QRegularExpression ordinal(QStringLiteral("^(iii|ii|i|first|second|third|1st|2nd|3rd)\\s+"));
    static const QRegularExpression numbered(QStringLiteral("^([123])\\s*"));
    static const QHash<QString, QString> ordinals{{"i", "1"}, {"ii", "2"}, {"iii", "3"}, {"first", "1"},
                                                  {"second", "2"}, {"third", "3"}, {"1st", "1"}, {"2nd", "2"}, {"3rd", "3"}};
    QString s = name.toLower().replace(space, QStringLiteral(" ")).trimmed();
    if (const auto m = ordinal.match(s); m.hasMatch())
        s = ordinals.value(m.captured(1)) + QLatin1Char(' ') + s.mid(m.capturedEnd());
    return s.replace(numbered, QStringLiteral("\\1 "));
}

const QHash<QString, const Book *> &byName()
{
    static const QHash<QString, const Book *> map = [] {
        QHash<QString, const Book *> m;
        for (const Book &b : books()) {
            m.insert(fold(QString::fromLatin1(b.name)), &b);
            for (const QString &alias : b.aliases)
                m.insert(fold(alias), &b);
        }
        return m;
    }();
    return map;
}

int letters(const QString &folded)
{
    int n = 0;
    for (QChar c : folded)
        n += c.isLetter();
    return n;
}

} // namespace

std::optional<ScriptureMention> findScriptureReference(const QString &text)
{
    // Chapter, optional :verse (or .verse), an optional range, then more parts after a comma
    // or semicolon: "3:16", "13:4-7", "3:16-4:2", "3:16, 18", "1:1-5; 2:1".
    static const QString dash = QStringLiteral("[-\\x{2010}-\\x{2015}\\x{2212}]");
    static const QString range = QStringLiteral("(?:\\s*%1\\s*(?:\\d+\\s*[:.]\\s*)?\\d+[a-z]?)?").arg(dash);
    static const QRegularExpression spec(
        QStringLiteral("(?<![\\p{L}\\d])(\\d+)(?:\\s*[:.]\\s*(\\d+)[a-z]?)?%1"
                       "(?:\\s*[,;]\\s*\\d+(?:\\s*[:.]\\s*\\d+)?[a-z]?%1)*(?![\\p{L}\\d])").arg(range),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\d]+"), QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression clock(QStringLiteral("^\\s*[ap]\\.?m\\b"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailing(QStringLiteral("[\\s.!]*$"));

    const qsizetype first = text.indexOf(QRegularExpression(QStringLiteral("\\S")));
    const qsizetype last = text.indexOf(trailing);   // where trailing spaces and stops begin

    for (auto specs = spec.globalMatch(text); specs.hasNext();) {
        const QRegularExpressionMatch m = specs.next();
        const qsizetype at = m.capturedStart();
        // The book is the last one to four words before the numbers, longest first.
        QList<qsizetype> starts;
        for (auto words = word.globalMatch(text.left(at)); words.hasNext();)
            starts << words.next().capturedStart();
        for (qsizetype k = qMin<qsizetype>(4, starts.size()); k >= 1; --k) {
            const qsizetype from = starts[starts.size() - k];
            const QString key = fold(text.mid(from, at - from));
            const Book *book = byName().value(key);
            if (!book)
                continue;
            const bool whole = from == first && m.capturedEnd() >= last;
            const bool hasVerse = m.hasCaptured(2);
            if (!whole) {
                const QString bare = QString(key).remove(QRegularExpression(QStringLiteral("^[123] ")));
                if (!hasVerse || letters(bare) < 3 || clock.match(text.mid(m.capturedEnd())).hasMatch())
                    break;
            }
            return ScriptureMention{text.mid(from, m.capturedEnd() - from).trimmed(), QString::fromLatin1(book->name),
                                    int(from)};
        }
    }
    return std::nullopt;
}

} // namespace visualis
