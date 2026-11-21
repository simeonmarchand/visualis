#pragma once

#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVideoSink>
#include <memory>
#include <vector>

// Plays the looping background into each output's VideoOutput sink.
//   shared:     one QMediaPlayer decodes into a hub sink; every frame is handed to all
//               output sinks (QVideoFrame copies share the buffer, so no pixel copy).
//   per-window: one QMediaPlayer per output, each writing straight to its sink.
class VideoEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(bool backgroundVisible READ backgroundVisible WRITE setBackgroundVisible NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double lastLoopGapMs READ lastLoopGapMs NOTIFY loopGapChanged)

public:
    explicit VideoEngine(const QString &mode, QObject *parent = nullptr);

    static VideoEngine *instance() { return s_instance; }
    static VideoEngine *create(QQmlEngine *, QJSEngine *engine);

    void addTarget(QVideoSink *sink, const QString &name);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool backgroundVisible() const { return m_backgroundVisible; }
    void setBackgroundVisible(bool visible);
    QString status() const { return m_status; }
    double lastLoopGapMs() const { return m_lastLoopGapMs; }
    int loopCount() const { return m_loopCount; }

signals:
    void changed();
    void loopGapChanged();

private:
    struct Target {
        QPointer<QVideoSink> sink;
        QString name;
    };
    struct Player {
        // Declared before player so the player is destroyed first and never writes into a dead hub.
        std::unique_ptr<QVideoSink> hub;   // shared mode only
        std::unique_ptr<QMediaPlayer> player;
        QString label;
        qint64 lastStartUs = -1;
        qint64 lastFrameNs = 0;
    };

    void rebuild();
    void watchFrames(Player *p, QVideoSink *sink);
    void setStatus(const QString &status);

    static VideoEngine *s_instance;
    std::vector<Target> m_targets;
    std::vector<std::unique_ptr<Player>> m_players;
    QElapsedTimer m_clock;
    QString m_mode;
    QUrl m_source;
    bool m_backgroundVisible = true;
    QString m_status = QStringLiteral("no video");
    double m_lastLoopGapMs = -1;
    int m_loopCount = 0;
};
