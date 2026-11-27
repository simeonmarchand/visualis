#include "library/templateeditor.h"

#include "library/templatelibrary.h"
#include "template/templatejson.h"

#include <QJsonObject>
#include <QLoggingCategory>
#include <QQmlEngine>

#include <cmath>

using namespace visualis;

Q_LOGGING_CATEGORY(lcTemplateEditor, "visualis.templates.editor")

namespace {

constexpr int kMaxRetryMs = 2000;   // how far retries back off while the library is busy

// The sample slide's group. Fixed, so every preview renders the same slide.
const QUuid kSampleGroup{QStringLiteral("{b0f9c2a4-6a52-4d1e-9a39-3f0c4a1d7e21}")};

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
    connect(this, &TemplateEditor::templateChanged, this, &TemplateEditor::previewChanged);
    connect(this, &TemplateEditor::sampleTextChanged, this, &TemplateEditor::previewChanged);
    load(builtInLyricsTemplate());
}

// At quit the outputs are closing too, so the last edit waits out another program's lock
// instead of being lost. Mid-service saves never wait (commit()).
TemplateEditor::~TemplateEditor()
{
    if (!m_pending)
        return;
    discardPending();
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);   // nothing to follow now
    const TemplateStore::Result r = m_library ? m_library->save(m_tmpl, TemplateStore::Lock::Wait)
                                              : TemplateStore::Result{false, QStringLiteral("the library isn't open")};
    if (!r.ok)
        qCWarning(lcTemplateEditor).noquote() << QStringLiteral("the last edit to \"%1\" was not saved: %2").arg(m_tmpl.name, r.error);
}

// Here, not in the constructor: the library comes from the QML engine, set after it.
void TemplateEditor::componentComplete()
{
    QQmlEngine *engine = qmlEngine(this);
    m_library = engine ? engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary") : nullptr;
    if (m_library) {
        // Before the new library's templates are read: an edit made in the old one stays there,
        // even if the new one has a template with the same id (a copy).
        connect(m_library, &TemplateLibrary::libraryChanged, this, &TemplateEditor::discardPending);
        connect(m_library, &TemplateLibrary::templatesChanged, this, &TemplateEditor::follow);
    }
    follow();
}

void TemplateEditor::follow()
{
    const TemplateSet set = m_library ? m_library->templates() : templateSet({});
    const auto it = set.constFind(m_tmpl.id);
    if (it == set.cend()) {
        // Removed, or another library: an edit waiting for it has nowhere to go.
        discardPending();
        load(builtInLyricsTemplate());
    } else if (!m_pending) {   // an edit waiting here wins over the copy just read
        const QString regionId = m_regionId;
        m_tmpl = *it;
        m_regionId = m_tmpl.region(regionId) ? regionId : m_tmpl.regions.value(0).id;
        emit templateChanged();
    }
}

void TemplateEditor::load(const Template &tmpl)
{
    m_tmpl = tmpl;
    m_regionId = m_tmpl.regions.value(0).id;
    emit templateChanged();
}

void TemplateEditor::setProblems(const QStringList &warnings, const QString &error)
{
    if (warnings == m_warnings && error == m_error)
        return;
    m_warnings = warnings;
    m_error = error;
    emit problemsChanged();
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
    if (!flush())
        return;   // stay with the edit that didn't save
    setProblems({}, {});
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
    setProblems({}, m_error);
    emit templateChanged();
}

QVariantMap TemplateEditor::style() const
{
    const TextRegion *r = m_tmpl.region(m_regionId);
    return r ? regionStyleToJson(r->style).toVariantMap() : QVariantMap();
}

double TemplateEditor::maxSafeArea()
{
    return templatelimits::kMaxSafeArea;
}

int TemplateEditor::maxTransitionMs()
{
    return templatelimits::kMaxTransitionMs;
}

QVariantMap TemplateEditor::transitions() const
{
    return {{"content", transitionToJson(m_tmpl.transitions.content).toVariantMap()},
            {"background", transitionToJson(m_tmpl.transitions.background).toVariantMap()}};
}

void TemplateEditor::setSampleText(const QString &text)
{
    if (text == m_sampleText)
        return;
    m_sampleText = text;
    emit sampleTextChanged();
}

Content TemplateEditor::preview() const
{
    // Through liveSong, as the outputs render it.
    Song sample;
    sample.title = QStringLiteral("Sample");
    sample.groups = {Group{kSampleGroup, {}, {}, {m_sampleText}}};
    sample.defaultTemplate = m_tmpl.id;
    TemplateSet set = m_library ? m_library->templates() : templateSet({});
    set.insert(m_tmpl.id, m_tmpl);
    return liveSong(sample, {}, set).slides.value(0).content.value_or(Content());
}

bool TemplateEditor::requireEditable()
{
    if (!builtIn())
        return true;
    setProblems({}, QStringLiteral("\"%1\" is built in and can't be changed. Duplicate it to make one you can.").arg(m_tmpl.name));
    return false;
}

void TemplateEditor::edited()
{
    setProblems({}, {});
    m_pending = true;
    if (!m_save.isActive())
        m_save.start();   // not restarted by later edits, so a long drag still saves as it goes
    emit templateChanged();
}

void TemplateEditor::discardPending()
{
    m_pending = false;
    m_save.stop();
}

void TemplateEditor::setStyle(const QVariantMap &properties)
{
    TextRegion *r = m_tmpl.region(m_regionId);
    if (!r || !requireEditable())
        return;
    QStringList warnings;
    const RegionStyle style = applyRegionStyle(r->style, QJsonObject::fromVariantMap(properties), r->id, &warnings);
    if (!warnings.isEmpty()) {
        setProblems(warnings, {});
        emit templateChanged();   // a control showing the refused value goes back
        return;
    }
    r->style = style;
    edited();
}

void TemplateEditor::setSafeArea(double x, double y)
{
    if (!requireEditable())
        return;
    auto valid = [](double v) { return std::isfinite(v) && v >= 0 && v <= templatelimits::kMaxSafeArea; };
    if (!valid(x) || !valid(y)) {
        setProblems({QStringLiteral("The safe area can be 0 to %1% of the output on each side.")
                         .arg(templatelimits::kMaxSafeArea * 100)}, {});
        emit templateChanged();
        return;
    }
    m_tmpl.safeAreaX = x;
    m_tmpl.safeAreaY = y;
    edited();
}

void TemplateEditor::setTransition(const QString &layer, const QVariantMap &change)
{
    if (!requireEditable())
        return;
    Transition *t = layer == QLatin1String("content")      ? &m_tmpl.transitions.content
                    : layer == QLatin1String("background") ? &m_tmpl.transitions.background
                                                           : nullptr;
    if (!t) {
        qCWarning(lcTemplateEditor).noquote() << QStringLiteral("\"%1\" isn't a layer").arg(layer.left(40));
        return;
    }
    QStringList warnings;
    const Transition next = applyTransition(*t, QJsonObject::fromVariantMap(change), layer, &warnings);
    if (!warnings.isEmpty()) {
        setProblems(warnings, {});
        emit templateChanged();   // a control showing the refused value goes back
        return;
    }
    *t = next;
    edited();
}

void TemplateEditor::dragFrame(const QRectF &start, int edges, double dx, double dy)
{
    TextRegion *r = m_tmpl.region(m_regionId);
    if (!r || !requireEditable())
        return;
    r->style.frame = draggedFrame(start, Qt::Edges(edges), {dx, dy});
    edited();
}

bool TemplateEditor::flush()
{
    return !m_pending || commit();
}

bool TemplateEditor::commit()
{
    discardPending();
    const TemplateStore::Result r = m_library ? m_library->save(m_tmpl)
                                              : TemplateStore::Result{false, QStringLiteral("The library isn't open.")};
    if (r.ok) {
        m_save.setInterval(kSaveDelayMs);
        setProblems({}, {});
        return true;
    }
    if (r.busy) {
        m_pending = true;
        m_save.setInterval(std::min(m_save.interval() * 2, kMaxRetryMs));
        m_save.start();
        setProblems({}, QStringLiteral("Not saved yet: %1. Trying again.").arg(r.error));
        return false;
    }
    // Show what the library has, so the editor never shows what didn't save.
    follow();
    setProblems({}, r.error);
    return false;
}

bool TemplateEditor::saveNew(Template &tmpl)
{
    tmpl.id = QUuid();
    tmpl.revision = 0;
    tmpl.created = tmpl.updated = {};
    const TemplateStore::Result r = m_library ? m_library->save(tmpl)
                                              : TemplateStore::Result{false, QStringLiteral("The library isn't open.")};
    if (!r.ok) {
        setProblems({}, r.error);
        return false;
    }
    setProblems({}, {});
    load(tmpl);
    return true;
}

QString TemplateEditor::create(const QString &name)
{
    if (!flush())
        return {};
    Template t = builtInLyricsTemplate();
    t.name = name.trimmed().isEmpty() ? QStringLiteral("New template") : name.trimmed();
    return saveNew(t) ? idText(t.id) : QString();
}

QString TemplateEditor::duplicate()
{
    if (!flush())
        return {};
    const QString suffix = QStringLiteral(" copy");
    Template t = m_tmpl;
    t.name = m_tmpl.name.left(templatelimits::kTextLength - suffix.size()) + suffix;
    return saveNew(t) ? idText(t.id) : QString();
}

void TemplateEditor::rename(const QString &name)
{
    if (!requireEditable() || name.trimmed().isEmpty() || name.trimmed() == m_tmpl.name)
        return;
    // Style edits first, so a name that's refused doesn't take them with it.
    const bool saved = flush();
    if (!saved && !m_pending)
        return;   // refused, and back to what's stored
    m_tmpl.name = name.trimmed();
    if (saved)
        commit();
    else
        emit templateChanged();   // the library is busy: saved with the edits on the retry
}

bool TemplateEditor::remove()
{
    if (!requireEditable())
        return false;
    const TemplateStore::Result r = m_library ? m_library->remove(m_tmpl.id)
                                              : TemplateStore::Result{false, QStringLiteral("The library isn't open.")};
    if (!r.ok) {
        setProblems({}, r.error);
        return false;
    }
    return true;   // follow() has gone back to the built-in
}
