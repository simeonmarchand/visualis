#pragma once

#include "template.h"

#include <QList>
#include <QStringList>
#include <QUuid>

#include <optional>

namespace visualis {

class Library;

struct TemplateSummary {
    QUuid id;
    QString name;
    TemplateKind kind = TemplateKind::Lyrics;
    bool builtIn = false;
};

// Reads and writes templates in a Library. Built-in templates aren't stored: load and list
// return them from code, and save and remove refuse them (they can only be duplicated, #94).
// Everything works on the Library's own connection, as SongStore does.
class TemplateStore
{
public:
    explicit TemplateStore(Library &library);

    struct Result {
        bool ok = true;
        QString error;    // for the operator
    };

    // Saves the template whole. Gives a new template an id, bumps its revision, and sets
    // the timestamps; on failure the caller's template is left as it was.
    Result save(Template &tmpl);
    // A stored template, or a built-in one. A stored one that reads with problems still loads,
    // with them in `warnings`.
    std::optional<Template> load(const QUuid &id, QStringList *warnings = nullptr) const;
    // False for a built-in template, or if there was no such template.
    bool remove(const QUuid &id);
    // Built-in templates first, then the stored ones by name, ignoring case. A template table
    // that can't be read still lists the built-ins.
    QList<TemplateSummary> list() const;

private:
    Library &m_library;
};

} // namespace visualis
