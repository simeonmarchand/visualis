#include "song/songimport.h"

namespace visualis {

ImportWriter::ImportWriter(SongStore &store, Duplicates rule)
    : m_store(store)
    , m_rule(rule)
{
    for (const SongSummary &s : store.list())
        m_titles.insert(s.title.trimmed().toLower());
}

void ImportWriter::add(ImportFile file)
{
    if (!file.error.isEmpty() || (!file.song && file.skipped.isEmpty())) {
        m_report.failed << std::pair{file.name, file.error};
        return;
    }
    if (!file.skipped.isEmpty()) {
        m_report.skipped << std::pair{file.name, file.skipped};
        return;
    }
    Song &song = *file.song;
    const QString title = song.title.trimmed().toLower();
    QStringList warnings;
    for (const QString &w : std::as_const(file.warnings))
        warnings << QStringLiteral("%1: %2").arg(file.name, w);
    if (m_rule == Duplicates::SkipSameTitle && m_titles.contains(title)) {
        m_report.skipped << std::pair{file.name, QStringLiteral("a song with this title is already in the library")};
        return;
    }
    if (m_rule == Duplicates::SkipSameIdWarnSameTitle) {
        if (!song.id.isNull() && m_store.load(song.id)) {
            m_report.skipped << std::pair{file.name, QStringLiteral("this song is already in the library")};
            return;
        }
        if (m_titles.contains(title))
            warnings << QStringLiteral("%1: a different song titled \"%2\" is already in the library; both are kept")
                            .arg(file.name, song.title);
    }
    if (const SongStore::Result saved = m_store.save(song); !saved.ok) {
        m_report.failed << std::pair{file.name, saved.error};
        return;
    }
    m_addedTitle << !m_titles.contains(title);
    m_titles.insert(title);
    m_report.imported << song.title;
    m_importedFiles << file.name;
    m_report.warnings << warnings;
}

void ImportWriter::mark()
{
    m_mark = m_report.imported.size();
    m_warningsMark = m_report.warnings.size();
}

void ImportWriter::batchFailed(const QString &error)
{
    // The songs saved since mark() were rolled back with the batch.
    for (qsizetype i = m_mark; i < m_report.imported.size(); ++i) {
        if (m_addedTitle[i])
            m_titles.remove(m_report.imported[i].trimmed().toLower());
        m_report.failed << std::pair{m_importedFiles[i], error};
    }
    m_report.imported.resize(m_mark);
    m_importedFiles.resize(m_mark);
    m_addedTitle.resize(m_mark);
    m_report.warnings.resize(m_warningsMark);
}

} // namespace visualis
