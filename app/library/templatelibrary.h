#pragma once

#include "template/template.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;
class SongLibrary;

// The library's templates as one snapshot (#93): what liveItem() renders with, built-ins
// included. SongLibrary owns it and it follows SongLibrary's library. Saving or removing a
// template here re-reads the snapshot and emits templatesChanged, and the service and what's
// live re-render from it. #94's editor writes through it.
class TemplateLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit TemplateLibrary(SongLibrary *songs);
    // The one SongLibrary owns, so QML and C++ share it.
    static TemplateLibrary *create(QQmlEngine *qml, QJSEngine *);

    // Every readable stored template, and the built-ins. A stored template that can't be read
    // is left out (and logged), so items using it fall back to the built-in.
    const visualis::TemplateSet &templates() const { return m_templates; }
    // The same, in the order editors list them: built-ins first, then by name.
    QList<visualis::Template> list() const;

    // As TemplateStore::save, and the snapshot is re-read. False, with `error`, when the
    // library isn't open or the save is refused.
    bool save(visualis::Template &tmpl, QString *error = nullptr);
    bool remove(const QUuid &id);

signals:
    void templatesChanged();

private:
    void reload();

    QPointer<SongLibrary> m_songs;
    visualis::TemplateSet m_templates;
};
