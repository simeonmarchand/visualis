#pragma once

#include "song/song.h"
#include "song/songstore.h"

#include <QFileInfoList>
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

    // Reads the library's titles once; songs this writer saves are added as it goes.
    ImportWriter(SongStore &store, Duplicates rule);

    // Saves the file's song, or records why not. Inside a SongStore::Batch the save isn't
    // durable until the batch commits; mark() and batchFailed() keep the report honest if
    // that commit fails.
    void add(ImportFile file);
    void mark();
    void batchFailed(const QString &error);

    const SongImportReport &report() const { return m_report; }

private:
    SongStore &m_store;
    Duplicates m_rule;
    QSet<QString> m_titles;   // lower case
    SongImportReport m_report;
    QStringList m_importedFiles;   // file name per entry of m_report.imported
    QList<bool> m_addedTitle;      // whether that entry's title was new to m_titles
    qsizetype m_mark = 0;
    qsizetype m_warningsMark = 0;
};

} // namespace visualis
