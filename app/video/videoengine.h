#pragma once

#include "output/platformdisplay.h"
#include "video/framewatchdog.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutex>
#include <QObject>
#include <QUrl>
#include <QVideoSink>

#include <memory>
#include <vector>

class QMediaPlayer;
class QRhi;

Q_DECLARE_LOGGING_CATEGORY(lcVideo)
// Frame and loop-gap measurements, off unless enabled:
//   QT_LOGGING_RULES="visualis.video.frames.debug=true"
Q_DECLARE_LOGGING_CATEGORY(lcVideoFrames)

// Plays the looping background into every output that shows it (ADR 0001): one decoder
// feeds a hub sink with its own GPU context, and each frame is handed to every output's
// sink. Frame copies share the GPU texture, so outputs always show the same frame and
// nothing is copied. Slide changes never touch it (plan 5.6).
//
// A watchdog restarts the player if frames stop arriving and tells the operator. While
// every display is asleep AVFoundation delivers no frames at all, so stalls then aren't
// failures, and the player restarts the moment the displays wake (#61).
//
// The background player owns one per slot (backgroundsource.h); nothing else sets its source.
class VideoEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    // For the operator: "No background", "Loading", "Playing", or what went wrong.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // A problem the operator should know about, e.g. a restart after a stall; empty when fine.
    Q_PROPERTY(QString alert READ alert NOTIFY statusChanged)
    Q_PROPERTY(int restartCount READ restartCount NOTIFY statusChanged)

public:
    explicit VideoEngine(QObject *parent = nullptr);
    ~VideoEngine() override;

    // An output's VideoOutput.videoSink. Targets that are destroyed drop out on their own.
    Q_INVOKABLE void addTarget(QVideoSink *sink);

    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QString status() const { return m_status; }
    QString alert() const { return m_alert; }
    int restartCount() const { return m_restartCount; }

    // Follows DisplaySleepWatcher; public so tests can simulate sleep and wake.
    void setDisplaysAsleep(bool asleep);

signals:
    void sourceChanged();
    void statusChanged();
    // The first frame after a start, or a restart, arrived.
    void playing();

private:
    QRhi *hubRhi();
    void startPlayer();
    void stopPlayer();
    void onFrame(const QVideoFrame &frame);   // decoder thread
    void onStalled(std::chrono::milliseconds silence);
    void setStatus(const QString &status, const QString &alert);

    // Read on the decoder thread for every frame, so guarded. Output windows live as long as
    // the app; a destroyed sink is removed (under the lock) before it's gone.
    QMutex m_targetsMutex;
    std::vector<QVideoSink *> m_targets;
    // Declared before the player so it outlives the decoder that renders with it.
    std::unique_ptr<QRhi> m_hubRhi;
    bool m_hubRhiFailed = false;
    std::unique_ptr<QVideoSink> m_hub;
    QMediaPlayer *m_player = nullptr;   // a child, so tests can find it
    visualis::FrameWatchdog m_watchdog;
    DisplaySleepWatcher m_sleepWatcher;
    bool m_displaysAsleep = false;
    QUrl m_source;
    QString m_status;
    QString m_alert;
    int m_restartCount = 0;
    int m_failuresInARow = 0;
    int m_generation = 0;   // a newer start cancels a pending delayed restart

    // Frame logging (decoder thread only).
    QElapsedTimer m_clock;
    qint64 m_lastStartUs = -1;
    qint64 m_lastFrameNs = 0;
    qint64 m_secondStartNs = 0;
    int m_framesThisSecond = 0;
    bool m_loggedFirstFrame = false;
};
