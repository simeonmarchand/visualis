#include "song/songimport.h"

namespace visualis {

namespace {
QString titleKey(const QString &title) { return title.trimmed().toLower(); }
} // namespace

ImportWriter::ImportWriter(SongStore &store, Duplicates rule)
    : m_store(store)
    , m_rule(rule)
{
    startBatch();
}

void ImportWriter::startBatch()
{
    m_titles.clear();
    for (const QString &title : m_store.titles())
        m_titles.insert(titleKey(title));
    m_batchTitles.clear();
    m_batchIds.clear();
    m_batchStart = m_entries.size();
}

void ImportWriter::add(ImportFile file)
{
    // What the parser noticed is kept whatever happens to the file next (#80).
    QStringList warnings;
    for (const QString &w : std::as_const(file.warnings))
        warnings << QStringLiteral("%1: %2").arg(file.name, w);
    if (!file.error.isEmpty() || (!file.song && file.skipped.isEmpty()))
        return record({file.name, Outcome::Failed, file.error, warnings});
    if (!file.skipped.isEmpty())
        return record({file.name, Outcome::Skipped, file.skipped, warnings});

    Song &song = *file.song;
    const QString title = titleKey(song.title);
    QString titleClash;
    if (m_rule == Duplicates::SkipSameTitle && m_titles.contains(title)) {
        return record({file.name, Outcome::Skipped, QStringLiteral("a song with this title is already in the library"),
                       warnings, {}, m_batchTitles.value(title, -1)});
    }
    if (m_rule == Duplicates::SkipSameIdWarnSameTitle) {
        if (!song.id.isNull() && (m_batchIds.contains(song.id) || m_store.load(song.id))) {
            return record({file.name, Outcome::Skipped, QStringLiteral("this song is already in the library"),
                           warnings, {}, m_batchIds.value(song.id, -1)});
        }
        if (m_titles.contains(title))
            titleClash = QStringLiteral("%1: a different song titled \"%2\" is already in the library; both are kept")
                             .arg(file.name, song.title);
    }
    if (const SongStore::Result saved = m_store.save(song); !saved.ok)
        return record({file.name, Outcome::Failed, saved.error, warnings});
    m_titles.insert(title);
    m_batchTitles.insert(title, m_entries.size());
    m_batchIds.insert(song.id, m_entries.size());
    record({file.name, Outcome::Imported, song.title, warnings, titleClash});
}

void ImportWriter::record(Entry entry)
{
    m_report.warnings << entry.warnings;
    switch (entry.outcome) {
    case Outcome::Imported:
        m_report.imported << entry.detail;
        // Only once it's saved: a song that isn't kept leaves nothing "both kept" (#80).
        if (!entry.titleClash.isEmpty())
            m_report.warnings << entry.titleClash;
        break;
    case Outcome::Skipped:
        m_report.skipped << std::pair{entry.name, entry.detail};
        break;
    case Outcome::Failed:
        m_report.failed << std::pair{entry.name, entry.detail};
        break;
    }
    m_entries << std::move(entry);
}

void ImportWriter::batchFailed(const QString &error)
{
    for (qsizetype i = m_batchStart; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        const bool rolledBack = e.outcome == Outcome::Imported;
        const bool skippedForOne = e.outcome == Outcome::Skipped && e.duplicateOf >= m_batchStart;
        if (rolledBack || skippedForOne) {
            e.outcome = Outcome::Failed;
            e.detail = error;
            e.titleClash.clear();
            e.duplicateOf = -1;
        }
    }
    rebuildReport();
    startBatch();   // the library's titles without the rolled-back songs
}

void ImportWriter::rebuildReport()
{
    const QList<Entry> entries = std::exchange(m_entries, {});
    m_report = {};
    for (const Entry &e : entries)
        record(e);
}

} // namespace visualis
