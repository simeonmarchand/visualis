#include "eventlog.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>

QFile EventLog::s_file;
QMutex EventLog::s_mutex;
QString EventLog::s_dir;

void EventLog::open(const QString &dir)
{
    QMutexLocker lock(&s_mutex);
    QDir().mkpath(dir);
    s_dir = dir;
    s_file.setFileName(dir + "/events.log");
    if (!s_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning() << "cannot open" << s_file.fileName();
}

void EventLog::write(const QString &message)
{
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + "  " + message;
    QMutexLocker lock(&s_mutex);
    qInfo().noquote() << line;
    if (s_file.isOpen()) {
        s_file.write(line.toUtf8() + '\n');
        s_file.flush();
    }
}

QString EventLog::dir()
{
    QMutexLocker lock(&s_mutex);
    return s_dir;
}
