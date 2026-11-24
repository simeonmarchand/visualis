#include "library/templatelibrary.h"

#include "library/songlibrary.h"
#include "template/templatestore.h"

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
    connect(songs, &SongLibrary::libraryChanged, this, &TemplateLibrary::reload);
    reload();
}

TemplateLibrary *TemplateLibrary::create(QQmlEngine *qml, QJSEngine *)
{
    TemplateLibrary *templates = qml->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary")->templateLibrary();
    QJSEngine::setObjectOwnership(templates, QJSEngine::CppOwnership);   // SongLibrary's child
    return templates;
}

QVariantList TemplateLibrary::entries() const
{
    QList<Template> stored;
    for (const Template &t : m_templates) {
        if (!isBuiltInTemplate(t.id))
            stored << t;
    }
    std::sort(stored.begin(), stored.end(), [](const Template &a, const Template &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    QVariantList list;
    for (const Template &t : builtInTemplates() + stored) {
        list.append(QVariantMap{{"id", t.id.toString(QUuid::WithoutBraces)}, {"name", t.name},
                                {"kind", templateKindName(t.kind)}, {"builtIn", isBuiltInTemplate(t.id)}});
    }
    return list;
}

bool TemplateLibrary::save(Template &tmpl, QString *error)
{
    Library *library = m_songs ? m_songs->library() : nullptr;
    if (!library) {
        if (error)
            *error = QStringLiteral("The library isn't open.");
        return false;
    }
    const TemplateStore::Result r = TemplateStore(*library).save(tmpl);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return false;
    }
    reload();
    return true;
}

bool TemplateLibrary::remove(const QUuid &id)
{
    Library *library = m_songs ? m_songs->library() : nullptr;
    if (!library || !TemplateStore(*library).remove(id))
        return false;
    reload();
    return true;
}

void TemplateLibrary::reload()
{
    QList<Template> stored;
    if (Library *library = m_songs ? m_songs->library() : nullptr) {
        const TemplateStore store(*library);
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
    emit templatesChanged();
}
