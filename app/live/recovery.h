#pragma once

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class VideoEngine;
namespace visualis {
class LiveState;
}

// Crash recovery (plan 5.9, #25). Writes what's live to a journal file on every change and
// deletes it on a clean quit, so a journal found at startup means the last run didn't end
// cleanly: the live song, slide, clears, and background come back, and the operator is told.
class Recovery : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // For the operator after a recovery; empty otherwise.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit Recovery(QObject *parent = nullptr) : QObject(parent) {}

    // Where the journal lives unless --journal says otherwise (Application Support).
    static QString defaultJournalPath();
    // A journal older than this is from another day's service and isn't restored.
    static constexpr int kMaxAgeHours = 12;

    // Restores from `journalPath` if the last run crashed, then keeps the journal from now
    // on. Returns true when something was restored.
    bool start(const QString &journalPath, visualis::LiveState *live, VideoEngine *video);
    // A clean quit: nothing to recover next time.
    void finish();

    QString message() const { return m_message; }
    Q_INVOKABLE void dismiss();

signals:
    void messageChanged();

private:
    void write();

    QString m_path;
    QPointer<visualis::LiveState> m_live;
    QPointer<VideoEngine> m_video;
    QString m_message;
    bool m_warnedWriteFailure = false;
};
