#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

// The one live state that every output renders (plan 5.2: outputs are views of
// one live state). Output windows hold no state of their own.
class LiveState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList slides READ slides CONSTANT)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(bool textCleared READ textCleared NOTIFY changed)
    Q_PROPERTY(QString currentText READ currentText NOTIFY changed)
    Q_PROPERTY(QString nextText READ nextText NOTIFY changed)
    Q_PROPERTY(QString itemTitle READ itemTitle CONSTANT)

public:
    explicit LiveState(QObject *parent = nullptr);

    static LiveState *instance() { return s_instance; }
    static LiveState *create(QQmlEngine *, QJSEngine *engine);

    QStringList slides() const { return m_slides; }
    int index() const { return m_index; }
    bool textCleared() const { return m_textCleared; }
    QString currentText() const;
    QString nextText() const;
    QString itemTitle() const { return QStringLiteral("Amazing Grace"); }

    // Every operator action is a named command (plan 5.8).
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void goTo(int index);
    Q_INVOKABLE void clearText();

signals:
    void changed();

private:
    void apply(const QString &command, int index, bool cleared);

    static LiveState *s_instance;
    QStringList m_slides;
    int m_index = 0;
    bool m_textCleared = false;
};
