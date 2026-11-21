#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

// Appends timestamped lines to events.log and echoes them to the console.
// Thread-safe: the render thread logs latency and the decoder logs loop gaps.
class EventLog
{
public:
    static void open(const QString &dir);
    static void write(const QString &message);
    static QString dir();

private:
    static QFile s_file;
    static QMutex s_mutex;
    static QString s_dir;
};
