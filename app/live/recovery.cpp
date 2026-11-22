#include "live/recovery.h"

#include "live/livejournal.h"
#include "live/livestate.h"
#include "video/videoengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

using namespace visualis;

Q_LOGGING_CATEGORY(lcRecovery, "visualis.recovery")

QString Recovery::defaultJournalPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("live-journal.json");
}

bool Recovery::start(const QString &journalPath, LiveState *live, VideoEngine *video)
{
    m_path = journalPath;
    m_live = live;
    m_video = video;
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    bool restored = false;
    const SnapshotResult found = readJournal(m_path);
    if (!found.error.isEmpty()) {
        qCWarning(lcRecovery).noquote() << "journal not used:" << found.error;
    } else if (found.snapshot && !found.snapshot->isEmpty()) {
        const LiveSnapshot &s = *found.snapshot;
        const qint64 ageSecs = s.savedAt.isValid() ? s.savedAt.secsTo(QDateTime::currentDateTimeUtc()) : -1;
        if (ageSecs < 0 || ageSecs > kMaxAgeHours * 3600) {
            qCInfo(lcRecovery) << "journal from an earlier service, not restored; saved" << s.savedAt;
        } else {
            // A journal holding a song too big to expand (#83) is refused, not allocated.
            QString refusal;
            const auto onRefused = connect(live, &LiveState::refused, this, [&refusal](const QString &r) { refusal = r; });
            if (s.song)
                live->restore(*s.song, s.arrangement, s.slideIndex, s.textCleared, s.backgroundCleared, s.service, s.item);
            else if (s.backgroundCleared)
                live->clearBackground();
            disconnect(onRefused);
            if (video && !s.background.isEmpty())
                video->setSource(QUrl(s.background));
            restored = true;
            if (!refusal.isEmpty()) {
                m_message = QStringLiteral("Visualis didn't quit normally last time, and what was live wasn't restored: %1")
                                .arg(refusal);
                qCWarning(lcRecovery).noquote() << "not restored:" << refusal;
            } else {
                const QString where = s.song
                    ? QStringLiteral("\"%1\", slide %2 of %3").arg(s.song->title).arg(live->slideIndex() + 1).arg(live->slideCount())
                    : QStringLiteral("the background");
                m_message = QStringLiteral("Visualis didn't quit normally last time. Back on %1 as of %2.")
                                .arg(where, s.savedAt.toLocalTime().toString("h:mm:ss AP"));
                qCInfo(lcRecovery).noquote() << "restored after a crash:" << where;
            }
            emit messageChanged();
        }
    }

    connect(live, &LiveState::changed, this, &Recovery::write);
    if (video)
        connect(video, &VideoEngine::sourceChanged, this, &Recovery::write);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &Recovery::finish);
    write();
    return restored;
}

void Recovery::write()
{
    if (!m_live || m_path.isEmpty())
        return;
    LiveSnapshot s;
    if (!m_live->songTitle().isEmpty() || !m_live->song().groups.isEmpty())
        s.song = m_live->song();
    s.arrangement = m_live->arrangementId();
    s.slideIndex = m_live->slideIndex();
    s.textCleared = m_live->textCleared();
    s.backgroundCleared = m_live->backgroundCleared();
    s.background = m_video ? m_video->source().toString() : QString();
    s.service = m_live->serviceId();
    s.item = m_live->itemId();
    s.savedAt = QDateTime::currentDateTimeUtc();

    if (s.isEmpty() && !s.backgroundCleared) {
        QFile::remove(m_path);   // nothing live: nothing to come back to
        return;
    }
    QString error;
    if (!writeJournal(m_path, s, &error) && !m_warnedWriteFailure) {
        // Recovery is a safety net; the service carries on without it.
        m_warnedWriteFailure = true;
        qCWarning(lcRecovery).noquote() << "can't write the journal" << m_path << ":" << error;
    }
}

void Recovery::finish()
{
    if (m_live)
        disconnect(m_live, nullptr, this, nullptr);
    if (m_video)
        disconnect(m_video, nullptr, this, nullptr);
    if (!m_path.isEmpty())
        QFile::remove(m_path);
}

void Recovery::dismiss()
{
    m_message.clear();
    emit messageChanged();
}
