#pragma once

#include "video/videoengine.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

class QVideoSink;

// What plays one slot of the background player (GLOSSARY.md "Background player"): a looping
// video or a still image. The player has a video and a still source for each of its two slots,
// and tests swap in fakes.
class BackgroundSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Starts on `path`: opening and playing a video, or decoding a still. ready() once its
    // first picture can be shown, or failed() if it never will. A load replaces the one before.
    virtual void load(const QString &path) = 0;
    // Stops playing or decoding and lets go of the picture.
    virtual void stop() = 0;

    // A video's output sinks; stills ignore them.
    virtual void addTarget(QVideoSink *) {}
    // A still's picture, empty until ready(); videos have none.
    virtual QImage image() const { return {}; }
    // For the operator, as VideoEngine's: what it's doing, and what went wrong.
    virtual QString status() const { return {}; }
    virtual QString alert() const { return {}; }

signals:
    void ready();
    void failed(const QString &why);
    void statusChanged();
};

// A looping video through its own VideoEngine (ADR 0001: one decoder per background). It's
// ready at its first frame, so a cut or a fade never shows a blank in between. A video that
// never delivers one (no output to show it in, or a file that won't play) stays loading, with
// VideoEngine's watchdog alert saying why.
class VideoSource : public BackgroundSource
{
    Q_OBJECT

public:
    explicit VideoSource(QObject *parent = nullptr);

    void load(const QString &path) override;
    void stop() override;
    void addTarget(QVideoSink *sink) override { m_engine.addTarget(sink); }
    QString status() const override { return m_engine.status(); }
    QString alert() const override { return m_engine.alert(); }

private:
    VideoEngine m_engine;
    bool m_loading = false;
};

struct StillImage {
    QImage image;
    QString error;   // for the operator; empty when it decoded
};

// Decodes `path` just large enough to cover `bound` (the output, in pixels), never larger than
// the image, and turned as its EXIF orientation says. A JPEG decodes at that size directly, so
// a 50-megapixel photo never sits in memory whole; formats that can't scale while decoding are
// read whole, up to medialimits::kImageAllocationMb, then scaled. Safe on any thread.
StillImage decodeStill(const QString &path, const QSize &bound);

// A still image, decoded on the thread pool so a large photo never holds up the outputs.
class StillSource : public BackgroundSource
{
    Q_OBJECT

public:
    explicit StillSource(QObject *parent = nullptr);

    void load(const QString &path) override;
    void stop() override;
    QImage image() const override { return m_image; }

    // The size stills decode at: the largest screen, in pixels, as output windows fill their
    // display.
    static QSize outputSize();

private:
    QImage m_image;
    quint64 m_generation = 0;   // a newer load or a stop makes an older decode's result stale
};
