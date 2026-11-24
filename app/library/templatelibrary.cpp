#include "library/templatelibrary.h"

#include "library/songlibrary.h"

#include <QJSEngine>
#include <QLoggingCategory>
#include <QQmlEngine>

#include <algorithm>

using namespace visualis;

Q_LOGGING_CATEGORY(lcTemplates, "visualis.templates")

TemplateLibrary::TemplateLibrary(SongLibrary *songs)
    : QObject(songs)
    , m_songs(songs)
    , m_templates(templateSet({}))
{
    connect(songs, &SongLibrary::libraryChanged, this, [this] {
        emit libraryChanged();
        reload();
    });
    reload();
}

TemplateLibrary *TemplateLibrary::create(QQmlEngine *qml, QJSEngine *)
{
    TemplateLibrary *templates = qml->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary")->templateLibrary();
    QJSEngine::setObjectOwnership(templates, QJSEngine::CppOwnership);   // SongLibrary's child
    return templates;
}

Library *TemplateLibrary::library() const
{
    return m_songs ? m_songs->library() : nullptr;
}

TemplateStore::Result TemplateLibrary::save(Template &tmpl, TemplateStore::Lock lock)
{
    Library *lib = library();
    if (!lib)
        return {false, QStringLiteral("The library isn't open.")};
    const TemplateStore::Result r = TemplateStore(*lib).save(tmpl, lock);
    if (r.ok)
        reload();
    return r;
}

TemplateStore::Result TemplateLibrary::remove(const QUuid &id)
{
    Library *lib = library();
    if (!lib)
        return {false, QStringLiteral("The library isn't open.")};
    const TemplateStore::Result r = TemplateStore(*lib).remove(id);
    if (r.ok)
        reload();
    return r;
}

void TemplateLibrary::reload()
{
    QList<Template> stored;
    if (Library *lib = library()) {
        const TemplateStore store(*lib);
        for (const TemplateSummary &summary : store.list()) {
            if (summary.builtIn)
                continue;
            QStringList warnings;
            const std::optional<Template> t = store.load(summary.id, &warnings);
            for (const QString &w : std::as_const(warnings))
                qCWarning(lcTemplates).noquote() << QStringLiteral("template \"%1\":").arg(summary.name) << w;
            if (t)
                stored << *t;
        }
    }
    m_templates = templateSet(stored);

    std::sort(stored.begin(), stored.end(), [](const Template &a, const Template &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    QVariantList entries;
    for (const Template &t : builtInTemplates() + stored) {
        entries.append(QVariantMap{{"id", t.id.toString(QUuid::WithoutBraces)}, {"name", t.name},
                                   {"kind", templateKindName(t.kind)}, {"builtIn", isBuiltInTemplate(t.id)}});
    }
    const bool listChanged = entries != m_entries;
    m_entries = entries;
    emit templatesChanged();
    if (listChanged)
        emit entriesChanged();
}
