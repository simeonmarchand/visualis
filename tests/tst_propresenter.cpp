#include "import/propresenter.h"
#include "import/protobuf.h"
#include "import/rtf.h"
#include "library/library.h"
#include "song/songstore.h"

#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

using namespace visualis;

// Builds ProPresenter-shaped protobuf bytes, so tests need no real (church, copyrighted) files.
namespace pb {

QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        quint8 b = v & 0x7F;
        v >>= 7;
        out += char(v ? b | 0x80 : b);
    } while (v);
    return out;
}
QByteArray bytes(int field, const QByteArray &data) { return varint(quint64(field) << 3 | 2) + varint(data.size()) + data; }
QByteArray str(int field, const QString &s) { return bytes(field, s.toUtf8()); }
QByteArray num(int field, quint64 v) { return varint(quint64(field) << 3) + v; }
QByteArray number(int field, quint64 v) { return varint(quint64(field) << 3) + varint(v); }
QByteArray f32(int field, float f)
{
    char raw[4];
    std::memcpy(raw, &f, 4);
    return varint(quint64(field) << 3 | 5) + QByteArray(raw, 4);
}
QByteArray uuid(int field, const QString &id) { return bytes(field, str(1, id)); }

QByteArray rtf(const QString &text)
{
    QString body;
    for (const QChar c : text) {
        if (c == '\n') body += "\\\n";
        else if (c.unicode() > 127) body += QStringLiteral("\\uc0\\u%1 ").arg(int(c.unicode()));
        else body += c;
    }
    return ("{\\rtf1\\ansi\\ansicpg1252\\cocoartf2761 {\\fonttbl\\f0\\fswiss Helvetica;}"
            "{\\colortbl;\\red255\\green255\\blue255;}\\pard\\qc\\f0\\fs120 \\cf1 " + body + "}").toUtf8();
}

// Cue { uuid=1, actions=10 { slide=23 { presentation=2 { base_slide=1 { elements=1 { element=1 { text=13 { rtf=5 }}}}}}}}
QByteArray cue(const QString &id, const QString &text)
{
    const QByteArray textMsg = bytes(5, rtf(text));
    const QByteArray element = bytes(1, bytes(13, textMsg));
    const QByteArray slide = bytes(1, element);
    const QByteArray action = bytes(23, bytes(2, bytes(1, slide)));
    return uuid(1, id) + bytes(10, action);
}

QByteArray color(float r, float g, float b, float a) { return f32(1, r) + f32(2, g) + f32(3, b) + f32(4, a); }

QByteArray cueGroup(const QString &id, const QString &name, const QByteArray &colorMsg, const QStringList &cueIds)
{
    QByteArray group = uuid(1, id) + str(2, name) + (colorMsg.isEmpty() ? QByteArray() : bytes(3, colorMsg));
    QByteArray out = bytes(1, group);
    for (const QString &c : cueIds)
        out += uuid(2, c);
    return out;
}

QByteArray arrangement(const QString &id, const QString &name, const QStringList &groupIds)
{
    QByteArray out = uuid(1, id) + str(2, name);
    for (const QString &g : groupIds)
        out += uuid(3, g);
    return out;
}

} // namespace pb

namespace {

// A small song (public-domain text): Intro (blank), Verse 1 (2 slides), Chorus; two arrangements.
QByteArray sampleSong()
{
    QByteArray p = pb::str(3, "Untitled");   // the name ProPresenter often stores; the file name wins
    p += pb::uuid(10, "arr-2");             // selected arrangement
    p += pb::bytes(11, pb::arrangement("arr-1", "Full", {"g-intro", "g-verse", "g-chorus", "g-chorus"}));
    p += pb::bytes(11, pb::arrangement("arr-2", "Short ", {"g-verse", "g-chorus"}));
    p += pb::bytes(12, pb::cueGroup("g-intro", "Intro", pb::color(0.7f, 0.65f, 0.14f, 1), {"c0"}));
    p += pb::bytes(12, pb::cueGroup("g-verse", "Verse 1", pb::color(0, 0.47f, 0.8f, 1), {"c1", "c2"}));
    p += pb::bytes(12, pb::cueGroup("g-chorus", "Chorus", pb::color(0, 0, 0, 0), {"c3"}));   // no color
    p += pb::bytes(13, pb::cue("c0", ""));
    p += pb::bytes(13, pb::cue("c1", "Joy to the world\nThe Lord is come"));
    p += pb::bytes(13, pb::cue("c2", "Let earth receive her King"));
    p += pb::bytes(13, pb::cue("c3", "And heaven and nature sing — ¡Gloria!"));
    p += pb::bytes(14, pb::str(1, "Isaac Watts") + pb::str(4, "Public Domain") + pb::number(5, 1719)
                       + pb::number(6, 12345));
    return p;
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QStringList labels(const Song &s)
{
    QStringList out;
    for (const Group &g : s.groups)
        out << g.label;
    return out;
}

} // namespace

class ProPresenterTest : public QObject
{
    Q_OBJECT

private slots:
    void readsGroupsSlidesAndColors()
    {
        const propresenter::ReadResult r = propresenter::readBytes(sampleSong(), "Joy to the World");
        QVERIFY(r.song);
        QVERIFY(r.error.isEmpty());
        QVERIFY(r.looksLikeSong);
        const Song &s = *r.song;
        QCOMPARE(s.title, QStringLiteral("Joy to the World"));
        QCOMPARE(labels(s), (QStringList{"Intro", "Verse 1", "Chorus"}));
        QCOMPARE(s.groups[0].slides, QStringList{""});
        QCOMPARE(s.groups[1].slides, (QStringList{"Joy to the world\nThe Lord is come", "Let earth receive her King"}));
        QCOMPARE(s.groups[2].slides, QStringList{QString::fromUtf8("And heaven and nature sing — ¡Gloria!")});
        QCOMPARE(s.groups[0].color, QStringLiteral("#b2a624"));   // 0.7f * 255 = 178.49...
        QCOMPARE(s.groups[1].color, QStringLiteral("#0078cc"));
        QCOMPARE(s.groups[2].color, QString());   // transparent means no color
        QVERIFY(r.warnings.isEmpty());
    }

    void readsArrangementsAndTheSelectedOne()
    {
        const Song s = *propresenter::readBytes(sampleSong(), "Joy").song;
        QCOMPARE(s.arrangements.size(), 2);
        QCOMPARE(s.arrangements[0].name, QStringLiteral("Full"));
        QCOMPARE(s.arrangements[1].name, QStringLiteral("Short"));   // trailing space trimmed
        const QList<QUuid> full = s.arrangements[0].sequence;
        QCOMPARE(full.size(), 4);
        QCOMPARE(full[2], full[3]);                 // the chorus repeats as one group
        QCOMPARE(full[1], s.groups[1].id);
        QCOMPARE(s.defaultArrangement, s.arrangements[1].id);
    }

    void readsCredits()
    {
        const Song s = *propresenter::readBytes(sampleSong(), "Joy").song;
        QCOMPARE(s.credits.ccliNumber, QStringLiteral("12345"));
        QCOMPARE(s.credits.authors, QStringLiteral("Isaac Watts"));
        QCOMPARE(s.credits.copyright, QStringLiteral("1719 Public Domain"));
    }

    void givesFreshIdsEachTime()
    {
        // Copied .pro files share internal ids; each import must still be its own song.
        const Song a = *propresenter::readBytes(sampleSong(), "Joy").song;
        const Song b = *propresenter::readBytes(sampleSong(), "Joy").song;
        QVERIFY(a.groups[0].id != b.groups[0].id);
        QVERIFY(a.arrangements[0].id != b.arrangements[0].id);
    }

    void slidesOutsideGroupsAreKept()
    {
        QByteArray p = pb::bytes(12, pb::cueGroup("g", "Verse 1", {}, {"c1"}));
        p += pb::bytes(13, pb::cue("c1", "Grouped"));
        p += pb::bytes(13, pb::cue("c2", "Loose"));
        const propresenter::ReadResult r = propresenter::readBytes(p, "Song");
        QCOMPARE(labels(*r.song), (QStringList{"Verse 1", ""}));
        QCOMPARE(r.song->groups[1].slides, QStringList{"Loose"});
        QCOMPARE(r.warnings.size(), 1);
    }

    void stepsAndCuesThatPointNowhereAreLeftOut()
    {
        QByteArray p = pb::bytes(11, pb::arrangement("a", "A", {"g", "missing-group"}));
        p += pb::bytes(12, pb::cueGroup("g", "Chorus", {}, {"c1", "missing-cue"}));
        p += pb::bytes(13, pb::cue("c1", "Sing"));
        const propresenter::ReadResult r = propresenter::readBytes(p, "Song");
        QCOMPARE(r.song->arrangements[0].sequence.size(), 1);
        QCOMPARE(r.song->groups[0].slides, QStringList{"Sing"});
        QCOMPARE(r.warnings.size(), 2);
    }

    void presentationsThatAreNotSongs()
    {
        // A sermon or scripture presentation: one unnamed group of text slides.
        QByteArray p = pb::bytes(12, pb::cueGroup("g", "", {}, {"c1"}));
        p += pb::bytes(13, pb::cue("c1", "In the beginning was the Word"));
        const propresenter::ReadResult r = propresenter::readBytes(p, "Sermon");
        QVERIFY(r.song);
        QVERIFY(!r.looksLikeSong);
    }

    void damagedFilesFailWithoutCrashing()
    {
        const QByteArray good = sampleSong();
        QVERIFY(!propresenter::readBytes({}, "x").song);
        QVERIFY(!propresenter::readBytes("not protobuf at all \xff\xff\xff\xff", "x").song);
        // Every truncation of a valid file either reads or fails cleanly.
        for (int cut = 1; cut < good.size(); cut += 7)
            propresenter::readBytes(good.left(cut), "x");
        // Random bytes, and a valid file with bytes flipped.
        QRandomGenerator rng(42);
        for (int i = 0; i < 300; ++i) {
            QByteArray noise(rng.bounded(1, 400), '\0');
            for (char &c : noise) c = char(rng.bounded(256));
            propresenter::readBytes(noise, "x");
            QByteArray flipped = good;
            for (int j = 0; j < 5; ++j) flipped[rng.bounded(int(flipped.size()))] = char(rng.bounded(256));
            propresenter::readBytes(flipped, "x");
        }
        QVERIFY(true);   // reaching here means no crash
    }

    void importsAFolder()
    {
        QTemporaryDir files, libDir;
        write(files.filePath("Joy to the World.pro"), sampleSong());
        QByteArray sermon = pb::bytes(12, pb::cueGroup("g", "", {}, {"c"})) + pb::bytes(13, pb::cue("c", "Notes"));
        write(files.filePath("Sunday Sermon.pro"), sermon);
        write(files.filePath("Broken.pro"), "\x0a\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff");
        write(files.filePath("readme.txt"), "not a presentation");

        auto lib = Library::open(libDir.path()).library;
        SongStore store(*lib);
        const propresenter::ImportReport first = propresenter::importFolder(files.path(), store);
        QCOMPARE(first.imported, QStringList{"Joy to the World"});
        QCOMPARE(first.skipped.size(), 1);
        QCOMPARE(first.skipped[0].first, QStringLiteral("Sunday Sermon.pro"));
        QCOMPARE(first.failed.size(), 1);
        QCOMPARE(first.failed[0].first, QStringLiteral("Broken.pro"));

        const QList<SongSummary> songs = store.list();
        QCOMPARE(songs.size(), 1);
        const Song saved = *store.load(songs[0].id);
        QCOMPARE(labels(saved), (QStringList{"Intro", "Verse 1", "Chorus"}));

        // Running it again adds nothing.
        const propresenter::ImportReport second = propresenter::importFolder(files.path(), store);
        QVERIFY(second.imported.isEmpty());
        QCOMPARE(store.list().size(), 1);
    }

    // #69: one huge or bogus file can't take all the memory.
    void aFileTooLargeToBeASongIsSkippedUnread()
    {
        QTemporaryDir files, libDir;
        write(files.filePath("Joy to the World.pro"), sampleSong());
        QFile huge(files.filePath("Huge.pro"));
        QVERIFY(huge.open(QIODevice::WriteOnly));
        QVERIFY(huge.resize(propresenter::kMaxFileBytes + 1));   // sparse: no disk used
        huge.close();

        auto lib = Library::open(libDir.path()).library;
        SongStore store(*lib);
        const propresenter::ImportReport r = propresenter::importFolder(files.path(), store);
        QCOMPARE(r.imported, QStringList{"Joy to the World"});
        QCOMPARE(r.skipped.size(), 1);
        QCOMPARE(r.skipped[0].first, QStringLiteral("Huge.pro"));
        QCOMPARE(propresenter::readFile(huge.fileName()).error, QStringLiteral("too large to be a song"));
    }

    void importReportsProgressAndCanBeStopped()
    {
        QTemporaryDir files, libDir;
        for (int i = 0; i < 4; ++i)
            write(files.filePath(QString("Song %1.pro").arg(i)), sampleSong());
        auto lib = Library::open(libDir.path()).library;
        SongStore store(*lib);
        QList<std::pair<int, int>> calls;
        const propresenter::ImportReport r = propresenter::importFolder(files.path(), store, [&](int done, int total) {
            calls << std::pair{done, total};
            return done < 2;   // stop before the third file
        });
        QCOMPARE(calls, (QList<std::pair<int, int>>{{0, 4}, {1, 4}, {2, 4}}));
        QCOMPARE(r.imported, (QStringList{"Song 0", "Song 1"}));
        QCOMPARE(store.list().size(), 2);
    }

    void rtfConversion_data()
    {
        QTest::addColumn<QByteArray>("rtf");
        QTest::addColumn<QString>("text");
        QTest::newRow("cocoa line break") << QByteArray("{\\rtf1 one\\\ntwo}") << "one\ntwo";
        QTest::newRow("par") << QByteArray("{\\rtf1 one\\par two}") << "one\ntwo";
        QTest::newRow("unicode apostrophe") << QByteArray("{\\rtf1 can\\uc0\\u8217 t}") << QString::fromUtf8("can’t");
        QTest::newRow("unicode with fallback char") << QByteArray("{\\rtf1 caf\\u233?}") << QString::fromUtf8("café");
        QTest::newRow("negative unicode") << QByteArray("{\\rtf1 \\uc0\\u-3913 }") << QString(QChar(char16_t(0xF0B7)));
        QTest::newRow("cp1252 hex") << QByteArray("{\\rtf1 \\'93quoted\\'94 Caf\\'e9}") << QString::fromUtf8("“quoted” Café");
        QTest::newRow("escaped symbols") << QByteArray("{\\rtf1 \\{x\\} \\\\ y}") << "{x} \\ y";
        QTest::newRow("font and color tables dropped")
            << QByteArray("{\\rtf1{\\fonttbl\\f0 Helvetica;}{\\colortbl;\\red0;}{\\*\\expandedcolortbl;;}Hi}") << "Hi";
        QTest::newRow("trailing spaces and blank edges") << QByteArray("{\\rtf1 \\par Hi   \\par \\par}") << "Hi";
        QTest::newRow("lone backslash at end") << QByteArray("{\\rtf1 x\\") << "x";
        QTest::newRow("unbalanced braces") << QByteArray("}}{\\rtf1 x") << "x";
    }
    void rtfConversion()
    {
        QFETCH(QByteArray, rtf);
        QFETCH(QString, text);
        QCOMPARE(rtfToText(rtf), text);
    }

    void protobufBasics()
    {
        const QByteArray buf = pb::str(3, "Hi") + pb::bytes(12, pb::number(2, 1)) + pb::number(4, 300) + pb::f32(7, 0.5f);
        const std::optional<ProtoMessage> m = ProtoMessage::parse(buf);
        QVERIFY(m);
        QCOMPARE(m->string(3), QStringLiteral("Hi"));
        QCOMPARE(m->message(12).varint(2), 1u);
        QCOMPARE(m->varint(4), 300u);
        QCOMPARE(*m->real(7), 0.5);
        QVERIFY(!m->has(99));
        QVERIFY(!ProtoMessage::parse(pb::varint(3 << 3 | 2) + pb::varint(50) + "short"));   // length past the end
        QVERIFY(!ProtoMessage::parse(QByteArray(11, char(0xFF))));                          // endless varint
    }
};

QTEST_GUILESS_MAIN(ProPresenterTest)
#include "tst_propresenter.moc"
