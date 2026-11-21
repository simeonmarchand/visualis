#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

// Runs operator commands (core/live/commands.h) from QML: keys, buttons, and later MIDI all
// come through here, so each is logged once and its output latency measured.
class Commands : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Commands(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool run(const QString &name, const QVariant &arg = {});
};
