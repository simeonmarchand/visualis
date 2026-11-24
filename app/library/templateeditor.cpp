#include "library/templateeditor.h"

#include "library/templatelibrary.h"
#include "template/templatejson.h"

#include <QJsonObject>
#include <QQmlEngine>

#include <cmath>

using namespace visualis;

namespace {

constexpr double kMaxSafeArea = 0.45;   // as templatejson reads it

QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

} // namespace

TemplateEditor::TemplateEditor(QObject *parent)
    : QObject(parent)
    , m_sampleText(QStringLiteral("Amazing grace, how sweet the sound\nThat saved a wretch like me"))
{
    m_save.setSingleShot(true);
    m_save.setInterval(kSaveDelayMs);
    connect(&m_save, &QTimer::timeout, this, &TemplateEditor::flush);
    load(builtInLyricsTemplate());
}

TemplateEditor::~TemplateEditor()
{
    flush();
}

// Here, not in the constructor: the library comes from the QML engine, set after it.
void TemplateEditor::componentComplete()
{
    QQmlEngine *engine = qmlEngine(this);
    m_library = engine ? engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary") : nullptr;
    if (m_library)
        connect(m_library, &TemplateLibrary::templatesChanged, this, &TemplateEditor::follow);
    follow();
}

void TemplateEditor::follow()
{
    const TemplateSet set = m_library ? m_library->templates() : templateSet({});
    const auto it = set.constFind(m_tmpl.id);
    if (it == set.cend()) {
        // Removed, or another library: an edit waiting for it has nowhere to go.
        m_pending = false;
        m_save.stop();
        load(builtInLyricsTemplate());
    } else if (m_pending) {
        emit changed();   // the list; the edit waiting here wins over the copy just read
    } else {
        const QString regionId = m_regionId;
        m_tmpl = *it;
        m_regionId = m_tmpl.region(regionId) ? regionId : m_tmpl.regions.value(0).id;
        emit changed();
    }
}

void TemplateEditor::load(const Template &tmpl)
{
    m_tmpl = tmpl;
    m_regionId = m_tmpl.regions.value(0).id;
    m_warnings.clear();
    m_error.clear();
    emit changed();
}

QVariantList TemplateEditor::templates() const
{
    QVariantList list;
    for (const Template &t : m_library ? m_library->list() : builtInTemplates())
        list.append(QVariantMap{{"id", idText(t.id)}, {"name", t.name}, {"builtIn", isBuiltInTemplate(t.id)}});
    return list;
}

QString TemplateEditor::templateId() const
{
    return idText(m_tmpl.id);
}

void TemplateEditor::select(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (uuid == m_tmpl.id || !m_library || !m_library->templates().contains(uuid))
        return;
    flush();
    load(m_library->templates().value(uuid));
}

bool TemplateEditor::builtIn() const
{
    return isBuiltInTemplate(m_tmpl.id);
}

QVariantList TemplateEditor::regions() const
{
    QVariantList list;
    for (const TextRegion &r : m_tmpl.regions)
        list.append(QVariantMap{{"id", r.id}, {"name", r.name}});
    return list;
}

void TemplateEditor::selectRegion(const QString &id)
{
    if (id == m_regionId || !m_tmpl.region(id))
        return;
    m_regionId = id;
    m_warnings.clear();
    emit changed();
}

TextRegion *TemplateEditor::region()
{
    for (TextRegion &r : m_tmpl.regions) {
        if (r.id == m_regionId)
            return &r;
    }
    return nullptr;
}

QVariantMap TemplateEditor::style() const
{
    const TextRegion *r = m_tmpl.region(m_regionId);
    return r ? regionStyleToJson(r->style).toVariantMap() : QVariantMap();
}

void TemplateEditor::setSampleText(const QString &text)
{
    if (text == m_sampleText)
        return;
    m_sampleText = text;
    emit changed();
}

Content TemplateEditor::preview() const
{
    // Through liveSong, as the outputs render it.
    Song sample;
    sample.title = QStringLiteral("Sample");
    sample.groups = {Group{QUuid(QStringLiteral("{b0f9c2a4-6a52-4d1e-9a39-3f0c4a1d7e21}")), {}, {}, {m_sampleText}}};
    sample.defaultTemplate = m_tmpl.id;
    TemplateSet set = m_library ? m_library->templates() : templateSet({});
    set.insert(m_tmpl.id, m_tmpl);
    return liveSong(sample, {}, set).slides.value(0).content;
}

bool TemplateEditor::editable()
{
    if (builtIn()) {
        m_error = QStringLiteral("\"%1\" is built in and can't be changed. Duplicate it to make one you can.").arg(m_tmpl.name);
        emit changed();
        return false;
    }
    return true;
}

void TemplateEditor::edited()
{
    m_warnings.clear();
    m_pending = true;
    if (!m_save.isActive())
        m_save.start();   // not restarted by later edits, so a long drag still saves as it goes
    emit changed();
}

void TemplateEditor::setStyle(const QVariantMap &properties)
{
    TextRegion *r = region();
    if (!r || !editable())
        return;
    QStringList warnings;
    const RegionStyle style = applyRegionStyle(r->style, QJsonObject::fromVariantMap(properties), r->id, &warnings);
    if (!warnings.isEmpty()) {
        m_warnings = warnings;
        emit changed();
        return;
    }
    r->style = style;
    edited();
}

void TemplateEditor::setSafeArea(double x, double y)
{
    if (!editable())
        return;
    auto valid = [](double v) { return std::isfinite(v) && v >= 0 && v <= kMaxSafeArea; };
    if (!valid(x) || !valid(y)) {
        m_warnings = {QStringLiteral("The safe area can be 0 to %1% of the output on each side.").arg(kMaxSafeArea * 100)};
        emit changed();
        return;
    }
    m_tmpl.safeAreaX = x;
    m_tmpl.safeAreaY = y;
    edited();
}

void TemplateEditor::dragFrame(const QRectF &start, int edges, double dx, double dy)
{
    TextRegion *r = region();
    if (!r || !editable())
        return;
    r->style.frame = draggedFrame(start, Qt::Edges(edges), {dx, dy});
    edited();
}

void TemplateEditor::flush()
{
    if (!m_pending)
        return;
    m_pending = false;
    m_save.stop();
    if (!saveNow(m_tmpl) && m_library) {
        // What the library has, so the editor never shows what didn't save.
        const QString error = m_error;
        follow();
        m_error = error;
        emit changed();
    }
}

bool TemplateEditor::saveNow(Template &tmpl)
{
    QString error;
    if (!m_library || !m_library->save(tmpl, &error)) {
        m_error = QStringLiteral("Couldn't save: %1").arg(error.isEmpty() ? QStringLiteral("no library") : error);
        emit changed();
        return false;
    }
    m_error.clear();
    emit changed();
    return true;
}

QString TemplateEditor::create(const QString &name)
{
    flush();
    Template t = builtInLyricsTemplate();
    t.id = QUuid();
    t.revision = 0;
    t.name = name.trimmed().isEmpty() ? QStringLiteral("New template") : name.trimmed();
    if (!saveNow(t))
        return {};
    load(t);
    return idText(t.id);
}

QString TemplateEditor::duplicate()
{
    flush();
    Template t = m_tmpl;
    t.id = QUuid();
    t.revision = 0;
    t.created = t.updated = {};
    t.name = m_tmpl.name.left(templatelimits::kTextLength - 5) + QStringLiteral(" copy");
    if (!saveNow(t))
        return {};
    load(t);
    return idText(t.id);
}

void TemplateEditor::rename(const QString &name)
{
    if (!editable() || name.trimmed().isEmpty() || name.trimmed() == m_tmpl.name)
        return;
    m_tmpl.name = name.trimmed();
    m_pending = true;
    flush();
}

bool TemplateEditor::remove()
{
    if (!editable())
        return false;
    m_pending = false;
    m_save.stop();
    if (!m_library || !m_library->remove(m_tmpl.id)) {
        m_error = QStringLiteral("Couldn't delete \"%1\".").arg(m_tmpl.name);
        emit changed();
        return false;
    }
    return true;   // follow() has gone back to the built-in
}
