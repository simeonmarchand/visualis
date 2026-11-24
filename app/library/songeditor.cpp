#include "library/songeditor.h"

#include "library/songlibrary.h"
#include "live/liveitem.h"
#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songlimits.h"
#include "song/songtext.h"
#include "template/overrides.h"
#include "template/templatejson.h"

#include <QLoggingCategory>
#include <QQmlEngine>

#include <algorithm>

using namespace visualis;

Q_LOGGING_CATEGORY(lcSongEditor, "visualis.songs.editor")

namespace {

constexpr int kMaxRetryMs = 2000;   // how far retries back off while the library is busy

QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

// "groupId/index", for QML.
QString slideKey(const QUuid &group, int index)
{
    return idText(group) + QLatin1Char('/') + QString::number(index);
}

} // namespace

SongEditor::SongEditor(QObject *parent)
    : QObject(parent)
{
    m_styleSave.setSingleShot(true);
    m_styleSave.setInterval(kSaveDelayMs);
    connect(&m_styleSave, &QTimer::timeout, this, &SongEditor::flush);
    connect(this, &SongEditor::changed, this, &SongEditor::selectionChanged);
}

// At quit the outputs are closing too, so the last style edit waits out another program's
// lock instead of being lost. Mid-service saves never wait (flush()).
SongEditor::~SongEditor()
{
    if (!m_stylePending)
        return;
    if (m_songUnsaved) {
        qCWarning(lcSongEditor).noquote() << QStringLiteral("the last edits to \"%1\" were not saved: %2").arg(m_song.title, m_error);
        return;
    }
    discardPendingStyle();
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);   // nothing to follow now
    const SongStore::Result r = m_library ? m_library->saveSlideOverrides(m_song, Library::Transaction::Lock::Wait)
                                          : SongStore::Result{false, QStringLiteral("the library isn't open")};
    if (!r.ok)
        qCWarning(lcSongEditor).noquote() << QStringLiteral("the last style edit to \"%1\" was not saved: %2").arg(m_song.title, r.error);
}

SongLibrary *SongEditor::library() const
{
    QQmlEngine *engine = qmlEngine(this);
    return engine ? engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary") : nullptr;
}

QString SongEditor::songId() const
{
    return idText(m_song.id);
}

void SongEditor::setSongId(const QString &id)
{
    SongLibrary *lib = library();
    // Here, not in the constructor: the library comes from the QML engine, set after it.
    if (lib && !m_followsTemplates) {
        connect(lib, &SongLibrary::templatesChanged, this, [this] {   // the preview, and the region edited
            fitRegion();
            emit changed();
        });
        m_followsTemplates = true;
        m_library = lib;
    }
    flush();
    if (m_stylePending && !m_songUnsaved)
        return;   // stay with the style edit the busy library hasn't taken yet
    if (m_stylePending)
        qCWarning(lcSongEditor).noquote() << QStringLiteral("the last edits to \"%1\" were not saved: %2").arg(m_song.title, m_error);
    discardPendingStyle();
    m_songUnsaved = false;
    const std::optional<Song> song = lib ? lib->loadSong(QUuid::fromString(id)) : std::nullopt;
    m_song = song.value_or(Song{});
    m_selected.clear();
    fitRegion();
    m_arrangement = m_song.defaultArrangement.isNull() && !m_song.arrangements.isEmpty()
        ? m_song.arrangements.first().id : m_song.defaultArrangement;
    // Kept as stored, but left out of what shows: say so.
    Song shown = m_song;
    m_warnings = dropOversizedOverrides(shown);
    m_error = song || id.isEmpty() ? QString() : QStringLiteral("That song is no longer in the library.");
    emit changed();
    emit lyricsReset();
}

void SongEditor::save()
{
    SongLibrary *lib = library();
    QString error;
    m_songUnsaved = !lib || !lib->saveSong(m_song, &error);
    if (m_songUnsaved)
        m_error = QStringLiteral("Couldn't save: %1").arg(error.isEmpty() ? QStringLiteral("no library") : error);
    else
        m_error.clear();
    // Saved with the rest; or, if that failed, left for the next save that works.
    if (m_songUnsaved)
        m_styleSave.stop();
    else
        discardPendingStyle();
    pruneSelection();
    emit changed();
}

QString SongEditor::lyrics() const
{
    return lyricsText(m_song);
}

void SongEditor::setTitle(const QString &title)
{
    if (!loaded() || title.trimmed().isEmpty() || title.trimmed() == m_song.title)
        return;
    m_song.title = title.trimmed();
    save();
}

void SongEditor::setCredits(const QString &ccliNumber, const QString &authors, const QString &copyright)
{
    const Credits credits{ccliNumber.trimmed(), authors.trimmed(), copyright.trimmed()};
    if (!loaded() || credits == m_song.credits)
        return;
    m_song.credits = credits;
    save();
}

void SongEditor::applyLyrics(const QString &text)
{
    if (!loaded() || text == lyricsText(m_song))
        return;
    LyricsEdit edit = applyLyricsText(m_song, text);
    m_song = edit.song;
    m_warnings = edit.warnings;
    if (!m_arrangement.isNull() && !m_song.arrangement(m_arrangement))
        m_arrangement = QUuid();
    save();
}

QVariantList SongEditor::groups() const
{
    QVariantList list;
    for (const Group &g : m_song.groups) {
        const bool arranged = m_song.arrangements.isEmpty()
            || std::any_of(m_song.arrangements.cbegin(), m_song.arrangements.cend(),
                           [&](const Arrangement &a) { return a.sequence.contains(g.id); });
        list.append(QVariantMap{{"id", idText(g.id)}, {"label", g.label}, {"color", g.color},
                                {"slideCount", g.slides.size()}, {"arranged", arranged}});
    }
    return list;
}

void SongEditor::renameGroup(const QString &id, const QString &label)
{
    Group *g = m_song.group(QUuid::fromString(id));
    if (!g || label.trimmed().isEmpty() || label.trimmed() == g->label)
        return;
    g->label = label.trimmed();
    save();
    emit lyricsReset();   // the header in the text changes too
}

void SongEditor::setGroupColor(const QString &id, const QString &color)
{
    Group *g = m_song.group(QUuid::fromString(id));
    if (!g || color == g->color)
        return;
    g->color = color;
    save();
}

void SongEditor::removeGroup(const QString &id)
{
    if (!m_song.group(QUuid::fromString(id)))
        return;
    m_song.removeGroup(QUuid::fromString(id));
    save();
    emit lyricsReset();
}

QVariantList SongEditor::arrangements() const
{
    QVariantList list;
    const QUuid def = m_song.defaultArrangement.isNull() && !m_song.arrangements.isEmpty()
        ? m_song.arrangements.first().id : m_song.defaultArrangement;
    for (const Arrangement &a : m_song.arrangements)
        list.append(QVariantMap{{"id", idText(a.id)}, {"name", a.name}, {"isDefault", a.id == def}});
    return list;
}

QString SongEditor::arrangementId() const
{
    return idText(m_arrangement);
}

void SongEditor::selectArrangement(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (uuid == m_arrangement || (!uuid.isNull() && !m_song.arrangement(uuid)))
        return;
    m_arrangement = uuid;
    emit changed();
}

Arrangement *SongEditor::shownArrangement(bool create)
{
    for (Arrangement &a : m_song.arrangements) {
        if (a.id == m_arrangement)
            return &a;
    }
    if (!create || !loaded())
        return nullptr;
    Arrangement a{QUuid::createUuid(), QStringLiteral("Default"), {}, {}};
    for (const Group &g : m_song.groups)
        a.sequence << g.id;
    m_song.arrangements << a;
    m_arrangement = a.id;
    return &m_song.arrangements.last();
}

QVariantList SongEditor::sequence() const
{
    QVariantList list;
    for (const QUuid &id : m_song.playOrder(m_arrangement)) {
        if (const Group *g = m_song.group(id))
            list.append(QVariantMap{{"groupId", idText(g->id)}, {"label", g->label}, {"color", g->color}});
    }
    return list;
}

QVariantList SongEditor::previewSlides() const
{
    // Through the song's template, as it will show (#93).
    SongLibrary *songs = library();
    const QList<LiveSlide> slides = liveSong(m_song, m_arrangement, songs ? songs->templates() : templateSet({})).slides;
    QVariantList list = slideMaps(slides);
    for (int i = 0; i < list.size(); ++i) {
        const SongSlide &s = slides[i].slide;
        const Group *g = m_song.group(s.groupId);
        QVariantMap map = list[i].toMap();
        map.insert("key", slideKey(s.groupId, s.indexInGroup));
        map.insert("overrides", g ? overrides::propertyCount(g->slideOverride(s.indexInGroup)) : 0);
        list[i] = map;
    }
    return list;
}

Template SongEditor::songTemplate() const
{
    SongLibrary *songs = library();
    return chooseTemplate(songs ? songs->templates() : templateSet({}), m_song.defaultTemplate).tmpl;
}

QVariantList SongEditor::regions() const
{
    QVariantList list;
    for (const TextRegion &r : songTemplate().regions)
        list.append(QVariantMap{{"id", r.id}, {"name", r.name}});
    return list;
}

void SongEditor::selectRegion(const QString &id)
{
    if (id == m_regionId || !songTemplate().region(id))
        return;
    m_regionId = id;
    emit selectionChanged();
}

void SongEditor::fitRegion()
{
    const Template t = songTemplate();
    if (!t.region(m_regionId))
        m_regionId = t.regions.value(0).id;
}

QStringList SongEditor::selectedKeys() const
{
    QStringList keys;
    for (const SlideRef &s : m_selected)
        keys << slideKey(s.group, s.index);
    return keys;
}

const Group *SongEditor::groupOf(const SlideRef &slide) const
{
    const Group *g = m_song.group(slide.group);
    return g && slide.index >= 0 && slide.index < g->slides.size() ? g : nullptr;
}

void SongEditor::selectSlide(const QString &key, bool extend)
{
    const QStringList parts = key.split(QLatin1Char('/'));
    bool ok = false;
    const SlideRef slide{QUuid::fromString(parts.value(0)), parts.value(1).toInt(&ok)};
    if (parts.size() != 2 || !ok || !groupOf(slide))
        return;
    if (!extend)
        m_selected = {slide};
    else if (!m_selected.removeOne(slide))
        m_selected << slide;
    emit selectionChanged();
}

void SongEditor::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit selectionChanged();
}

void SongEditor::pruneSelection()
{
    m_selected.removeIf([this](const SlideRef &s) { return !groupOf(s); });
}

QList<QJsonObject> SongEditor::selectedOverrides() const
{
    QList<QJsonObject> out;
    for (const SlideRef &s : m_selected) {
        if (const Group *g = groupOf(s))
            out << g->slideOverride(s.index);
    }
    return out;
}

QVariantMap SongEditor::slideStyle() const
{
    const QList<QJsonObject> selected = selectedOverrides();
    if (selected.isEmpty())
        return {};
    const std::optional<RegionStyle> style = effectiveStyle(songTemplate(), m_regionId, selected.first());
    return style ? regionStyleToJson(*style).toVariantMap() : QVariantMap();
}

QStringList SongEditor::overriddenProperties() const
{
    QStringList out;
    for (const QJsonObject &o : selectedOverrides())
        out << overrides::overriddenProperties(o, m_regionId);
    out.removeDuplicates();
    out.sort();
    return out;
}

QStringList SongEditor::mixedProperties() const
{
    const Template t = songTemplate();
    QList<QJsonObject> styles;
    for (const QJsonObject &o : selectedOverrides()) {
        if (const std::optional<RegionStyle> style = effectiveStyle(t, m_regionId, o))
            styles << regionStyleToJson(*style);
    }
    QStringList out;
    for (const QString &path : overrides::regionProperties()) {
        for (const QJsonObject &style : styles) {
            if (overrides::propertyValue(style, path) != overrides::propertyValue(styles.first(), path)) {
                out << path;
                break;
            }
        }
    }
    return out;
}

QVariantMap SongEditor::slideTransition() const
{
    const QList<QJsonObject> selected = selectedOverrides();
    if (selected.isEmpty())
        return {};
    return transitionToJson(effectiveTransition(songTemplate().transitions.content, {selected.first()})).toVariantMap();
}

bool SongEditor::slideTransitionOverridden() const
{
    const QList<QJsonObject> selected = selectedOverrides();
    return std::any_of(selected.cbegin(), selected.cend(), overrides::overridesTransition);
}

bool SongEditor::slideTransitionMixed() const
{
    const QList<QJsonObject> selected = selectedOverrides();
    const Transition content = songTemplate().transitions.content;
    return std::any_of(selected.cbegin(), selected.cend(), [&](const QJsonObject &o) {
        return effectiveTransition(content, {o}) != effectiveTransition(content, {selected.first()});
    });
}

int SongEditor::maxTransitionMs()
{
    return templatelimits::kMaxTransitionMs;
}

void SongEditor::editSelected(const std::function<QString(QJsonObject &)> &edit)
{
    if (!loaded())
        return;
    const Template t = songTemplate();
    QList<std::pair<SlideRef, QJsonObject>> edited;
    QStringList problems;
    for (const SlideRef &s : std::as_const(m_selected)) {
        const Group *g = groupOf(s);
        if (!g)
            continue;
        const QJsonObject was = g->slideOverride(s.index);
        QJsonObject next = was;
        if (const QString why = edit(next); !why.isEmpty()) {
            problems << why;
            continue;
        }
        // Rules between properties, on the style it would show with. A problem it already had
        // (an imported override) isn't this edit's.
        QStringList before, after;
        effectiveStyle(t, m_regionId, was, &before);
        effectiveStyle(t, m_regionId, next, &after);
        for (const QString &w : std::as_const(after)) {
            if (!before.contains(w))
                problems << w;
        }
        if (next != was)
            edited.append({s, next});
    }
    if (!problems.isEmpty()) {
        problems.removeDuplicates();
        m_warnings = problems;
        emit changed();   // a control showing the refused value goes back
        return;
    }
    if (edited.isEmpty())
        return;
    for (const auto &[slide, next] : std::as_const(edited)) {
        Group *g = m_song.group(slide.group);
        g->fitSlideOverrides();
        g->slideOverrides[slide.index] = next;
    }
    styleEdited();
}

void SongEditor::setSlideProperty(const QString &path, const QVariant &value)
{
    const QJsonValue v = QJsonValue::fromVariant(value);
    editSelected([&](QJsonObject &o) { return overrides::setProperty(o, m_regionId, path, v); });
}

void SongEditor::resetSlideProperty(const QString &path)
{
    editSelected([&](QJsonObject &o) {
        overrides::resetProperty(o, m_regionId, path);
        return QString();
    });
}

void SongEditor::resetSlides()
{
    editSelected([](QJsonObject &o) {
        overrides::resetAll(o);
        return QString();
    });
}

void SongEditor::setSlideTransition(const QString &kind, int ms)
{
    editSelected([&](QJsonObject &o) { return overrides::setTransition(o, {{"kind", kind}, {"ms", ms}}); });
}

void SongEditor::resetSlideTransition()
{
    editSelected([](QJsonObject &o) {
        overrides::resetTransition(o);
        return QString();
    });
}

void SongEditor::styleEdited()
{
    m_warnings.clear();
    m_stylePending = true;
    if (!m_styleSave.isActive())
        m_styleSave.start();   // not restarted by later edits, so a long drag still saves as it goes
    emit changed();
}

void SongEditor::discardPendingStyle()
{
    m_stylePending = false;
    m_styleSave.stop();
}

bool SongEditor::flush()
{
    if (!m_stylePending)
        return true;
    // An edit that didn't save (save()) is in m_song too. Saving only the overrides would be
    // refused against the stored slides and lose it; the next whole save takes both.
    if (m_songUnsaved) {
        m_styleSave.stop();
        return false;
    }
    discardPendingStyle();
    SongLibrary *lib = library();
    const SongStore::Result r = lib ? lib->saveSlideOverrides(m_song) : SongStore::Result{false, QStringLiteral("The library isn't open.")};
    if (r.ok) {
        m_styleSave.setInterval(kSaveDelayMs);
        m_error.clear();
        emit changed();
        return true;
    }
    if (r.busy) {
        m_stylePending = true;
        m_styleSave.setInterval(std::min(m_styleSave.interval() * 2, kMaxRetryMs));
        m_styleSave.start();
        m_error = QStringLiteral("Not saved yet: the library is in use by another program. Trying again.");
        emit changed();
        return false;
    }
    // Show what the library has, so the editor never shows what didn't save.
    const QString lyricsBefore = lyricsText(m_song);
    if (const std::optional<Song> stored = lib ? lib->loadSong(m_song.id) : std::nullopt)
        m_song = *stored;
    m_error = r.error;
    pruneSelection();
    emit changed();
    if (lyricsText(m_song) != lyricsBefore)
        emit lyricsReset();
    return false;
}

QString SongEditor::templateId() const
{
    return idText(m_song.defaultTemplate.isNull() ? builtInLyricsTemplateId() : m_song.defaultTemplate);
}

void SongEditor::setTemplateId(const QString &id)
{
    // The built-in is stored as none, so the song follows whatever the built-in is.
    QUuid uuid = QUuid::fromString(id);
    if (isBuiltInTemplate(uuid))
        uuid = QUuid();
    if (!loaded() || uuid == m_song.defaultTemplate)
        return;
    m_song.defaultTemplate = uuid;
    fitRegion();
    save();
}

QString SongEditor::addArrangement(const QString &name)
{
    if (!loaded())
        return {};
    Arrangement a{QUuid::createUuid(), name.trimmed().isEmpty() ? QStringLiteral("New arrangement") : name.trimmed(), {}, {}};
    for (const Group &g : m_song.groups)
        a.sequence << g.id;
    m_song.arrangements << a;
    m_arrangement = a.id;
    save();
    return idText(a.id);
}

void SongEditor::renameArrangement(const QString &id, const QString &name)
{
    for (Arrangement &a : m_song.arrangements) {
        if (a.id == QUuid::fromString(id) && !name.trimmed().isEmpty() && a.name != name.trimmed()) {
            a.name = name.trimmed();
            save();
            return;
        }
    }
}

void SongEditor::removeArrangement(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (!m_song.arrangement(uuid))
        return;
    m_song.arrangements.removeIf([&](const Arrangement &a) { return a.id == uuid; });
    if (m_song.defaultArrangement == uuid)
        m_song.defaultArrangement = QUuid();
    if (m_arrangement == uuid)
        m_arrangement = m_song.arrangements.isEmpty() ? QUuid() : m_song.arrangements.first().id;
    save();
}

void SongEditor::makeDefaultArrangement(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (!m_song.arrangement(uuid) || m_song.defaultArrangement == uuid)
        return;
    m_song.defaultArrangement = uuid;
    save();
}

void SongEditor::addStep(const QString &groupId)
{
    if (!m_song.group(QUuid::fromString(groupId)))
        return;
    if (Arrangement *a = shownArrangement(true)) {
        a->sequence << QUuid::fromString(groupId);
        save();
    }
}

void SongEditor::removeStep(int index)
{
    Arrangement *a = shownArrangement(true);
    if (!a || index < 0 || index >= a->sequence.size())
        return;
    a->sequence.removeAt(index);
    save();
}

void SongEditor::moveStep(int index, int delta)
{
    Arrangement *a = shownArrangement(true);
    const int to = index + delta;
    if (!a || index < 0 || index >= a->sequence.size() || to < 0 || to >= a->sequence.size())
        return;
    a->sequence.move(index, to);
    save();
}

bool SongEditor::deleteSong()
{
    SongLibrary *lib = library();
    if (!loaded() || !lib || !lib->removeSong(songId()))
        return false;
    discardPendingStyle();
    m_song = Song{};
    m_arrangement = QUuid();
    m_selected.clear();
    emit changed();
    emit lyricsReset();
    return true;
}
