#include "song/labels.h"

#include <QtTest>

using namespace visualis;

namespace {

SectionLabel L(const QString &kind, std::optional<int> number = std::nullopt, const QString &language = {})
{
    return SectionLabel{kind, number, language, true};
}

QList<Group> groups(const QStringList &labels)
{
    QList<Group> out;
    for (const QString &l : labels)
        out << Group{QUuid::createUuid(), l, {}, {}};
    return out;
}

// Group labels of the matched steps, "-" for unmatched, for readable comparisons.
QStringList labelsOf(const SequenceMatch &m, const QList<Group> &gs)
{
    QStringList out;
    for (const QUuid &id : m.steps) {
        auto it = std::find_if(gs.cbegin(), gs.cend(), [&](const Group &g) { return g.id == id; });
        out << (it == gs.cend() ? QStringLiteral("-") : it->label);
    }
    return out;
}

} // namespace

class LabelsTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesSpellingVariants_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<SectionLabel>("expected");
        QTest::newRow("pre chorus 1") << "Pre Chorus 1" << L("prechorus", 1);
        QTest::newRow("prechorus") << "PreChorus" << L("prechorus");
        QTest::newRow("pre-chorus 2") << "Pre-Chorus 2" << L("prechorus", 2);
        QTest::newRow("post chorus") << "Post Chorus 3" << L("postchorus", 3);
        QTest::newRow("refain typo") << "Refain 3" << L("refrain", 3);
        QTest::newRow("upper case") << "VAMP" << L("vamp");
        QTest::newRow("lower case") << "tag 2" << L("tag", 2);
        QTest::newRow("short verse") << "V1" << L("verse", 1);
        QTest::newRow("short chorus") << "C" << L("chorus");
        QTest::newRow("short pre-chorus") << "PC2" << L("prechorus", 2);
        QTest::newRow("spanish prefix") << "Spanish - Chorus 1" << L("chorus", 1, "spanish");
        QTest::newRow("accented") << "Español - Coro" << SectionLabel{"coro", std::nullopt, "spanish", false};
        QTest::newRow("tamil suffix") << "Bridge 1 Tamil" << L("bridge", 1, "tamil");
        QTest::newRow("tamil prefix") << "Tamil - PreChorus" << L("prechorus", std::nullopt, "tamil");
        QTest::newRow("spaces") << "  Verse   4  " << L("verse", 4);
    }
    void parsesSpellingVariants()
    {
        QFETCH(QString, text);
        QFETCH(SectionLabel, expected);
        QCOMPARE(parseLabel(text), expected);
    }

    void unknownAndEmptyLabels()
    {
        QCOMPARE(parseLabel(""), (SectionLabel{"", std::nullopt, "", false}));
        const SectionLabel spoken = parseLabel("Spoken Word");
        QVERIFY(!spoken.known);
        QCOMPARE(spoken.kind, QStringLiteral("spokenword"));
        QVERIFY(!parseLabel("Tug").known);            // short words never fuzzy-match ("Tag")
        QVERIFY(!parseLabel("Versus").known);         // two edits from "verse"
    }

    // Every group name in the church's ProPresenter library (spikes/propresenter, finding 4)
    // must parse to a known kind, except the unnamed groups.
    void everyChurchGroupNameIsKnown()
    {
        const QStringList names = {
            "Ending", "Verse 1", "Intro", "Verse 2", "Chorus", "Bridge", "Interlude", "Verse 3", "Refrain",
            "Outro", "Vamp", "Instrumental", "Tag", "Bridge 1", "Bridge 2", "Chorus 2", "Chorus 1",
            "Turnaround", "Verse 4", "PreChorus", "Refrain 2", "Breakdown", "Spanish - Chorus 1",
            "Tamil - Chorus", "Pre-Chorus 2", "Bridge 3", "Chorus 3", "Tamil - Verse 1", "Spanish - Verse 2",
            "Bridge 4", "tag 2", "Spanish - Chorus 2", "Chorus 4", "Spanish - Bridge", "Refain 3", "Verse",
            "Tamil - Verse 2", "Post Chorus 1", "Spanish - Verse 1", "Spanish - Verse 3", "Post Chorus",
            "Pre-Chorus 3", "Post Chorus 2", "Refrain 4", "Spanish - Chorus", "Tamil - PreChorus",
            "Tamil - Chorus 2", "Spanish - Verse 4", "Chorus 5", "Vamp 2", "Pre-Chorus 4", "Vamp 3",
            "Pre-Chorus 5", "Pre-Chorus 6", "Spanish - Tag", "VAMP", "Refrain 7", "Refrain 8", "Refrain 5",
            "Refrain 6", "Instrumental 2", "Blank", "Verse 5", "Tamil - Bridge", "Bridge 1 Tamil",
            "Bridge 1 Spanish", "Post Chorus 3", "Spanish - Bridge 1", "Spanish - Bridge 2"};
        QCOMPARE(names.size(), 69);   // 70 distinct names in the library, one of them empty
        for (const QString &n : names)
            QVERIFY2(parseLabel(n).known, qPrintable(n));
    }

    // The real case from P3 and the ProPresenter spike: Planning Center's sequence for
    // "I Thank God" against the church's ProPresenter groups. 34 of 36 steps match; Bridge 2
    // and 3 don't exist in ProPresenter, so they're left for the operator.
    void iThankGod()
    {
        const QList<Group> gs = groups({"Intro", "Verse 1", "Verse 2", "PreChorus", "Chorus", "Verse 3", "Verse 4",
                                        "Pre-Chorus 2", "Bridge 1", "Refrain", "Turnaround", "Interlude", "Ending"});
        const QStringList pco = {
            "Intro", "Verse 1", "Verse 2", "Pre Chorus 1", "Chorus", "Turnaround", "Verse 3", "Verse 4",
            "Pre Chorus 2", "Chorus", "Turnaround", "Turnaround", "Bridge 1", "Bridge 1", "Bridge 2", "Bridge 3",
            "Chorus", "Turnaround", "Turnaround", "Interlude", "Interlude", "Refrain", "Refrain", "Refrain",
            "Interlude", "Interlude", "Interlude", "Interlude", "Interlude", "Interlude", "Interlude", "Refrain",
            "Refrain", "Refrain", "Chorus", "Ending"};
        const SequenceMatch m = matchSequence(pco, gs);
        QCOMPARE(m.steps.size(), 36);
        QCOMPARE(m.steps.count(QUuid()), 2);
        QCOMPARE(m.unmatched, (QStringList{"Bridge 2", "Bridge 3"}));
        const QStringList got = labelsOf(m, gs);
        QCOMPARE(got[3], QStringLiteral("PreChorus"));
        QCOMPARE(got[8], QStringLiteral("Pre-Chorus 2"));
        QCOMPARE(got.mid(12, 4), (QStringList{"Bridge 1", "Bridge 1", "-", "-"}));
    }

    void numberingDifferences()
    {
        // The only group of a kind matches whether or not either side is numbered.
        const QList<Group> one = groups({"Chorus 1"});
        QCOMPARE(labelsOf(matchSequence({"Chorus"}, one), one), QStringList{"Chorus 1"});
        const QList<Group> bare = groups({"Chorus"});
        QCOMPARE(labelsOf(matchSequence({"Chorus 1", "Chorus 2"}, bare), bare), (QStringList{"Chorus", "Chorus"}));

        // With several, "X 1" and "X" pair up, and anything else must match exactly.
        const QList<Group> several = groups({"Chorus", "Chorus 2"});
        QCOMPARE(labelsOf(matchSequence({"Chorus 1", "Chorus 2", "Chorus 3"}, several), several),
                 (QStringList{"Chorus", "Chorus 2", "-"}));
        const QList<Group> numbered = groups({"Verse 1", "Verse 2"});
        QCOMPARE(labelsOf(matchSequence({"Verse", "Verse 3"}, numbered), numbered), (QStringList{"Verse 1", "-"}));

        // Two numbered groups and a bare label: ambiguous unless one is number 1.
        const QList<Group> noOne = groups({"Chorus 2", "Chorus 3"});
        const SequenceMatch m = matchSequence({"Chorus"}, noOne);
        QCOMPARE(m.unmatched, QStringList{"Chorus"});
    }

    void languagesAreKeptApart()
    {
        const QList<Group> gs = groups({"Chorus", "Verse 1", "Spanish - Chorus", "Spanish - Verse 1", "Bridge 1 Tamil"});
        QCOMPARE(labelsOf(matchSequence({"Chorus", "Verse 1", "Bridge 1"}, gs), gs), (QStringList{"Chorus", "Verse 1", "-"}));
        QCOMPARE(labelsOf(matchSequence({"Chorus", "Verse 1"}, gs, "spanish"), gs),
                 (QStringList{"Spanish - Chorus", "Spanish - Verse 1"}));
        QCOMPARE(labelsOf(matchSequence({"Bridge 1"}, gs, "tamil"), gs), QStringList{"Bridge 1 Tamil"});
    }

    void unmatchedListsEachLabelOnce()
    {
        const QList<Group> gs = groups({"Verse 1"});
        const SequenceMatch m = matchSequence({"Tag", "Verse 1", "Tag", "Outro"}, gs);
        QCOMPARE(m.unmatched, (QStringList{"Tag", "Outro"}));
        QCOMPARE(m.steps.count(QUuid()), 3);
    }
};

QTEST_GUILESS_MAIN(LabelsTest)
#include "tst_labels.moc"
