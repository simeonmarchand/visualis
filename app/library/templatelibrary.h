#pragma once

#include "template/template.h"
#include "template/templatestore.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
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
    // {id, name, kind, builtIn} for the editors' lists: built-ins first, then by name.
    // Signalled only when one of those changes, not on every save of a style, so a list
    // keeps its place while a template is being edited.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)

public:
    explicit TemplateLibrary(SongLibrary *songs);
    // The one SongLibrary owns, so QML and C++ share it.
    static TemplateLibrary *create(QQmlEngine *qml, QJSEngine *);

    // Every readable stored template, and the built-ins. A stored template that can't be read
    // is left out (and logged), so items using it fall back to the built-in.
    const visualis::TemplateSet &templates() const { return m_templates; }
    QVariantList entries() const { return m_entries; }

    // As TemplateStore's, and the snapshot is re-read. Not ok when the library isn't open,
    // the change is refused, or (`busy`) another program is writing to the library.
    visualis::TemplateStore::Result save(visualis::Template &tmpl,
                                         visualis::TemplateStore::Lock lock = visualis::TemplateStore::Lock::NowOrBusy);
    visualis::TemplateStore::Result remove(const QUuid &id);

signals:
    // Another library is open (or none): sent before its templates are read.
    void libraryChanged();
    void templatesChanged();
    void entriesChanged();

private:
    void reload();
    visualis::Library *library() const;

    QPointer<SongLibrary> m_songs;
    visualis::TemplateSet m_templates;
    QVariantList m_entries;
};
