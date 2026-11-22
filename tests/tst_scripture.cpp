#include "scripture/reference.h"

#include <QtTest>

using namespace visualis;

class ScriptureTest : public QObject
{
    Q_OBJECT

private slots:
    void findsReferences_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("reference");
        QTest::addColumn<QString>("book");

        QTest::newRow("bare") << "Acts 2:38" << "Acts 2:38" << "Acts";
        QTest::newRow("in a title (P3 finding 6)") << "Scripture Reading: Acts 2:38" << "Acts 2:38" << "Acts";
        QTest::newRow("range") << "Reading - Romans 8:28-39" << "Romans 8:28-39" << "Romans";
        QTest::newRow("en dash") << "Romans 8:28–39" << "Romans 8:28–39" << "Romans";
        QTest::newRow("across chapters") << "John 3:16-4:2" << "John 3:16-4:2" << "John";
        QTest::newRow("several parts") << "1 Cor 13:4-7; 14:1 (responsive)" << "1 Cor 13:4-7; 14:1" << "1 Corinthians";
        QTest::newRow("roman ordinal") << "Reading I Corinthians 13:4" << "I Corinthians 13:4" << "1 Corinthians";
        QTest::newRow("word ordinal") << "First John 1:9" << "First John 1:9" << "1 John";
        QTest::newRow("three-letter code") << "Sermon text: Rom 12:1-2" << "Rom 12:1-2" << "Romans";
        QTest::newRow("abbreviation with a stop") << "Matt. 5:1-12" << "Matt. 5:1-12" << "Matthew";
        QTest::newRow("whole psalm") << "Psalm 23" << "Psalm 23" << "Psalms";
        QTest::newRow("one-chapter book") << "Jude 3" << "Jude 3" << "Jude";
        QTest::newRow("short alias, whole title") << "Jn 3:16" << "Jn 3:16" << "John";
        QTest::newRow("multi-word name") << "Song of Songs 2:10" << "Song of Songs 2:10" << "Song of Solomon";
        QTest::newRow("verse letter") << "Mark 1:15a" << "Mark 1:15a" << "Mark";
        QTest::newRow("trailing stop") << "Hebrews 11:1." << "Hebrews 11:1" << "Hebrews";
    }

    void findsReferences()
    {
        QFETCH(QString, text);
        QFETCH(QString, reference);
        QFETCH(QString, book);
        const std::optional<ScriptureMention> m = findScriptureReference(text);
        QVERIFY2(m, qPrintable(text));
        QCOMPARE(m->reference, reference);
        QCOMPARE(m->book, book);
        QCOMPARE(text.mid(m->start, m->reference.size()), reference);
    }

    void ignoresOtherTitles_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("welcome") << "Welcome";
        QTest::newRow("header") << "Pre-Service";
        QTest::newRow("a time") << "Service is 10:30";
        QTest::newRow("a time after a book name") << "Mark 10:30 am";
        QTest::newRow("chapter only, in a longer title") << "Psalm 23 Choir";
        QTest::newRow("a count") << "Announcements 5 min";
        QTest::newRow("not a book") << "Hymn 23";
        QTest::newRow("short alias in a longer title") << "Prayer: Is 5:1";
        QTest::newRow("book name with no numbers") << "Acts";
        QTest::newRow("empty") << "";
        QTest::newRow("number glued to a word") << "Rom8";
    }

    void ignoresOtherTitles()
    {
        QFETCH(QString, text);
        const std::optional<ScriptureMention> m = findScriptureReference(text);
        QVERIFY2(!m, qPrintable(m ? m->reference : QString()));
    }
};

QTEST_GUILESS_MAIN(ScriptureTest)
#include "tst_scripture.moc"
