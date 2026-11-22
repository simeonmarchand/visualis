#pragma once

#include "song/song.h"
#include "song/songstore.h"

#include <QFileInfoList>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

namespace visualis {

// Importing a folder of songs happens in two steps (#77): each file is read on its own,
// which needs no library and can run on a worker thread, then an ImportWriter saves what
// was read, on the thread that owns the library's only writing connection.

// One file, read for an import and not yet saved.
struct ImportFile {
    QString name;               // the file name, for the report
    std::optional<Song> song;   // set when the file is a song
    QString skipped;            // why it was passed over (not a song, too large), if it was
    QString error;              // why it couldn't be read, if it couldn't
    QStringList warnings;       // reported with the file's name if the song is imported
};

struct SongImportReport {
    QStringList imported;                        // titles
    QList<std::pair<QString, QString>> skipped;  // file name, reason
    QList<std::pair<QString, QString>> failed;   // file name, error
    QStringList warnings;                        // "file: warning"
};

class ImportWriter
{
public:
    // What counts as already in the library.
    enum class Duplicates {
        SkipSameTitle,             // ProPresenter: files carry no ids, so the title decides
        SkipSameIdWarnSameTitle,   // Visualis exports: same id is the same song; same title may not be
    };

    ImportWriter(SongStore &store, Duplicates rule);

    // Starts a batch of saves (see SongStore::Batch): reads the library's titles again, so
    // songs created, renamed, or deleted since the last batch count. Nothing else writes
    // while a batch is open.
    void startBatch();
    // Saves the file's song, or records why not.
    void add(ImportFile file);
    // The batch's commit failed: the songs it saved weren't kept, and files skipped as
    // duplicates of them are reported failed too.
    void batchFailed(const QString &error);

    // Each list in file order.
    const SongImportReport &report() const { return m_report; }

private:
    enum class Outcome { Imported, Skipped, Failed };
    struct Entry {
        QString name;
        Outcome outcome;
        QString detail;          // the title if imported, else the reason
        QStringList warnings;    // "file: warning", reported if imported
        qsizetype duplicateOf = -1;   // a song saved earlier in the same batch
    };
    void record(Entry entry);
    void rebuildReport();

    SongStore &m_store;
    Duplicates m_rule;
    QSet<QString> m_titles;               // lower case: the library's, as of startBatch(), and since
    QHash<QString, qsizetype> m_batchTitles;   // lower-case title -> entry saved in this batch
    QHash<QUuid, qsizetype> m_batchIds;
    QList<Entry> m_entries;
    qsizetype m_batchStart = 0;
    SongImportReport m_report;
};

} // namespace visualis
