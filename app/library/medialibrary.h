#pragma once

#include "media/mediaasset.h"
#include "media/mediaresolve.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
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
//
// This relies on one Visualis process per library, which the library lock (#109) enforces:
// a second process can't open it, so the cross-process races in the #104 review (H2, M4, M5)
// can't happen. Within the process, the GUI thread is the only place that publishes and deletes.
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

    // Why an asset can't be deleted now (empty: it can), asked before every delete. The
    // background player says when it's showing or loading it (#126 review).
    void setInUse(std::function<QString(const QUuid &)> why) { m_inUse = std::move(why); }

    // How long a finished file waits for another program to let go of the library before
    // it's reported failed (#88's pattern). For tests.
    void setLockWaitLimit(int ms);
    // How long after Stop (or a library switch) a worker that hasn't stopped is left behind,
    // so another import can start (#115). For tests.
    void setAbandonAfter(int ms);
    // For tests: runs on the worker after each file is read, before it's handed over. It
    // stands in for a read that won't return.
    void setWorkerPause(std::function<void()> pause);

    // Every asset, whatever the search, and the media folder, as a value for the resolver
    // (mediaresolve.h). Changes with assetsChanged.
    visualis::MediaSnapshot snapshot() const { return m_snapshot; }

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
    // After a stop: leaves the worker behind if it hasn't finished in m_abandonAfterMs.
    void abandonIfStuck();
    void abandonWorker();
    void setReport(const QString &report);
    // Reads the assets again from the library, and emits assetsChanged.
    void reloadAssets();

    QPointer<SongLibrary> m_songs;
    std::unique_ptr<visualis::MediaStore> m_store;
    QList<visualis::MediaAsset> m_assets;
    visualis::MediaSnapshot m_snapshot;
    QString m_search;
    QString m_report;
    std::unique_ptr<ImportRun> m_import;
    QTimer m_importPoll;   // saves what the worker has finished and updates the progress
    int m_lockWaitLimitMs = 10'000;
    int m_abandonAfterMs = 2'000;   // a working copy sees Stop within one 1 MB read
    quint64 m_importGeneration = 0;   // tells a late abandon timer its import has ended
    std::function<void()> m_workerPause;
    std::function<QString(const QUuid &)> m_inUse;
};
