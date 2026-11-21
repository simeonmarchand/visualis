#include "rtf.h"

#include <QList>
#include <QSet>

namespace visualis {

namespace {

// Groups that hold metadata rather than text.
const QSet<QByteArray> &skippedDestinations()
{
    static const QSet<QByteArray> set = {"fonttbl", "colortbl", "expandedcolortbl", "stylesheet", "info",
                                         "pict", "listtable", "listoverridetable", "header", "footer"};
    return set;
}

// Windows-1252 differs from Latin-1 only in 0x80-0x9F.
QChar cp1252(quint8 b)
{
    static const char16_t high[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                      0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    return b >= 0x80 && b < 0xA0 ? QChar(high[b - 0x80]) : QChar(b);
}

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
int hexValue(char c)
{
    if (isDigit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

QString rtfToText(const QByteArray &rtf)
{
    QString out;
    struct State { bool skip; int uc; };
    QList<State> stack;
    State st{false, 1};
    int pendingSkip = 0;   // characters still to drop after a \uN escape
    auto put = [&](QChar c) {
        if (st.skip)
            return;
        if (pendingSkip > 0) {
            --pendingSkip;
            return;
        }
        out += c;
    };

    const qsizetype n = rtf.size();
    qsizetype i = 0;
    while (i < n) {
        const char c = rtf[i];
        if (c == '{') {
            stack << st;
            ++i;
            // A group whose first control word is a metadata destination is dropped whole.
            if (i + 1 < n && rtf[i] == '\\') {
                if (rtf[i + 1] == '*') {
                    st.skip = true;
                } else {
                    qsizetype j = i + 1;
                    while (j < n && isLetter(rtf[j]))
                        ++j;
                    if (skippedDestinations().contains(rtf.mid(i + 1, j - i - 1)))
                        st.skip = true;
                }
            }
        } else if (c == '}') {
            st = stack.isEmpty() ? State{false, 1} : stack.takeLast();
            ++i;
        } else if (c == '\\') {
            ++i;
            if (i >= n)
                break;
            const char d = rtf[i];
            if (isLetter(d)) {
                const qsizetype start = i;
                while (i < n && isLetter(rtf[i]))
                    ++i;
                const QByteArray word = rtf.mid(start, i - start);
                const qsizetype argStart = i;
                if (i < n && (rtf[i] == '-' || isDigit(rtf[i]))) {
                    ++i;
                    while (i < n && isDigit(rtf[i]))
                        ++i;
                }
                const QByteArray arg = rtf.mid(argStart, i - argStart);
                if (i < n && rtf[i] == ' ')
                    ++i;   // the delimiter space belongs to the control word
                if (st.skip)
                    continue;
                if (word == "par" || word == "line") {
                    put('\n');
                } else if (word == "tab") {
                    put('\t');
                } else if (word == "uc") {
                    st.uc = qMax(0, arg.toInt());
                } else if (word == "u") {
                    int code = arg.toInt();
                    if (code < 0)
                        code += 65536;
                    put(QChar(char16_t(code)));   // surrogate halves pair up in UTF-16
                    pendingSkip = st.uc;
                }
            } else if (d == '\'') {
                const int hi = i + 1 < n ? hexValue(rtf[i + 1]) : -1;
                const int lo = i + 2 < n ? hexValue(rtf[i + 2]) : -1;
                if (hi >= 0 && lo >= 0) {
                    put(cp1252(quint8(hi * 16 + lo)));
                    i += 3;
                } else {
                    ++i;
                }
            } else {
                ++i;
                if (d == '\\' || d == '{' || d == '}')
                    put(QChar(d));
                else if (d == '\n' || d == '\r')
                    put('\n');            // Cocoa writes "\<newline>" for a line break
                else if (d == '~')
                    put(QChar(0x00A0));
            }
        } else {
            ++i;
            if (c != '\n' && c != '\r')
                put(QChar::fromLatin1(c));
        }
    }

    out.replace(QChar(0x2028), QChar('\n'));
    out.replace(QChar(0x2029), QChar('\n'));
    QStringList lines = out.split('\n');
    for (QString &line : lines) {
        while (!line.isEmpty() && line.back().isSpace())
            line.chop(1);
    }
    QString text = lines.join('\n');
    while (text.startsWith('\n'))
        text.remove(0, 1);
    while (text.endsWith('\n'))
        text.chop(1);
    return text;
}

} // namespace visualis
