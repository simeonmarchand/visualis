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

    // Writes don't wait on another program's lock (#122 review): the editor saves from the
    // GUI thread during a service, so a busy library fails at once with `busy` set and the
    // caller tries again later.
    struct Result {
        bool ok = true;
        QString error;      // for the operator
        bool busy = false;  // another program holds the library's write lock; try again
    };

    // Whether a write waits out another program's lock (up to Library::kBusyTimeoutMs). Only
    // where nothing is on screen to freeze, e.g. the editor's last save at quit.
    enum class Lock { NowOrBusy, Wait };

    // Saves the template whole. Gives a new template an id, bumps its revision, and sets
    // the timestamps; on failure the caller's template is left as it was.
    Result save(Template &tmpl, Lock lock = Lock::NowOrBusy);
    // A stored template, or a built-in one. A stored one that reads with problems still loads,
    // with them in `warnings`.
    std::optional<Template> load(const QUuid &id, QStringList *warnings = nullptr) const;
    // Not ok for a built-in template, or if there was no such template.
    Result remove(const QUuid &id);
    // Built-in templates first, then the stored ones by name, ignoring case. A template table
    // that can't be read still lists the built-ins.
    QList<TemplateSummary> list() const;

private:
    Library &m_library;
};

} // namespace visualis
