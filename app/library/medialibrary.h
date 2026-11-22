#pragma once

#include "media/mediaasset.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

struct MediaImportFile;
class QJSEngine;
class QQmlEngine;
class QThread;
class SongLibrary;

namespace visualis {
class MediaStore;
}

// The media pane's view of the library's images and videos (#97): the searchable thumbnail
// grid, importing, and deleting. The library itself is SongLibrary's; this follows it.
//
// An import copies, hashes, probes, and thumbnails each file on a worker thread, into
// media/.partial/ only; the worker never opens the library or touches a published file. This
// object, on the GUI thread, is the one place that publishes copies (a rename), saves their
// rows, and deletes media, so those never race (#97 review). A save never waits for another
// program's lock: it's retried on the next poll until a deadline (#88).
class MediaLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY assetsChanged)
    // {id, name, kind ("image"|"video"), thumbnail (file URL, or empty), details} matching the
    // search, by name.
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QString report READ report NOTIFY reportChanged)
    Q_PROPERTY(QString reportSummary READ reportSummary NOTIFY reportChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QString importProgress READ importProgress NOTIFY importProgressChanged)

public:
    explicit MediaLibrary(SongLibrary *songs, QObject *parent = nullptr);
    ~MediaLibrary() override;
    static MediaLibrary *create(QQmlEngine *qml, QJSEngine *);

    // Imports the files, and the images and videos directly inside any folders. One import
    // at a time: false, with nothing started, while one runs or when no library is open.
    Q_INVOKABLE bool importFiles(const QList<QUrl> &urls);
    // Stops after the file in hand; a file being copied is abandoned and its copy removed.
    Q_INVOKABLE void stopImport();
    // The services that show the asset, so the pane can ask before deleting it.
    Q_INVOKABLE QStringList usedBy(const QString &id) const;
    Q_INVOKABLE bool removeAsset(const QString &id);

    // How long a finished file waits for another program to let go of the library before
    // it's reported failed (#88's pattern). For tests.
    void setLockWaitLimit(int ms);

    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList assets() const;
    QString report() const { return m_report; }
    QString reportSummary() const;
    bool importing() const { return m_import != nullptr; }
    QString importProgress() const;

signals:
    void assetsChanged();
    void reportChanged();
    void importingChanged();
    void importProgressChanged();
    void importFinished();

private:
    struct ImportState;
    struct ImportRun;
    void libraryChanged();
    void saveFinishedFiles();
    // Publishes and saves one file; false if the library is busy and it should be retried.
    bool saveOne(MediaImportFile &file);
    void finishImport();
    void setReport(const QString &report);

    QPointer<SongLibrary> m_songs;
    std::unique_ptr<visualis::MediaStore> m_store;
    QList<visualis::MediaAsset> m_assets;
    QString m_search;
    QString m_report;
    std::unique_ptr<ImportRun> m_import;
    QTimer m_importPoll;   // saves what the worker has finished and updates the progress
    int m_lockWaitLimitMs = 10'000;
};
