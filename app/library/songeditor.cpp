#include "library/songeditor.h"

#include "library/songlibrary.h"
#include "live/liveitem.h"
#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songtext.h"
#include "template/overrides.h"
#include "template/templatejson.h"

#include <QLoggingCategory>
#include <QQmlEngine>

using namespace visualis;

Q_LOGGING_CATEGORY(lcSongEditor, "visualis.songs.editor")

namespace {

constexpr int kMaxRetryMs = 2000;   // how far retries back off while the library is busy

QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

// A slide by its group and place in it, so a group's repeats in the play order are one slide.
QString slideKey(const QUuid &group, int index)
{
    return idText(group) + QLatin1Char('/') + QString::number(index);
}

std::pair<QUuid, int> fromSlideKey(const QString &key)
{
    const QStringList parts = key.split(QLatin1Char('/'));
    bool ok = false;
    const int index = parts.value(1).toInt(&ok);
    return {parts.size() == 2 && ok ? QUuid::fromString(parts[0]) : QUuid(), index};
}

// How many template properties an override sets, in every region.
int overrideCount(const QJsonObject &o)
{
    int n = 0;
    const QJsonObject regions = o.value("regions").toObject();
    for (auto it = regions.begin(); it != regions.end(); ++it)
        n += overrides::overriddenProperties(o, it.key()).size();
    return n;
}

} // namespace

SongEditor::SongEditor(QObject *parent)
    : QObject(parent)
{
    m_styleSave.setSingleShot(true);
    m_styleSave.setInterval(kSaveDelayMs);
    connect(&m_styleSave, &QTimer::timeout, this, &SongEditor::flush);
}

// At quit the outputs are closing too, so the last style edit waits out another program's
// lock instead of being lost. Mid-service saves never wait (flush()).
SongEditor::~SongEditor()
{
    if (!m_stylePending)
        return;
    discardPendingStyle();
    QString error = QStringLiteral("the library isn't open");
    if (!m_library || !m_library->saveSong(m_song, &error))
        qCWarning(lcSongEditor).noquote() << QStringLiteral("the last style edit to \"%1\" was not saved: %2").arg(m_song.title, error);
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
        connect(lib, &SongLibrary::templatesChanged, this, &SongEditor::changed);   // the preview
        m_followsTemplates = true;
        m_library = lib;
    }
    if (!flush())
        return;   // stay with the style edit that didn't save
    const std::optional<Song> song = lib ? lib->loadSong(QUuid::fromString(id)) : std::nullopt;
    m_song = song.value_or(Song{});
    m_selected.clear();
    m_regionId = QStringLiteral("main");
    m_arrangement = m_song.defaultArrangement.isNull() && !m_song.arrangements.isEmpty()
        ? m_song.arrangements.first().id : m_song.defaultArrangement;
    m_warnings.clear();
    m_error = song || id.isEmpty() ? QString() : QStringLiteral("That song is no longer in the library.");
    emit changed();
    emit lyricsReset();
}

void SongEditor::save()
{
    SongLibrary *lib = library();
    QString error;
    if (!lib || !lib->saveSong(m_song, &error)) {
        m_error = QStringLiteral("Couldn't save: %1").arg(error.isEmpty() ? QStringLiteral("no library") : error);
    } else {
        m_error.clear();
        discardPendingStyle();   // saved with the rest
    }
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
        map.insert("overrides", g ? overrideCount(g->slideOverride(s.indexInGroup)) : 0);
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
    m_warnings.clear();
    emit changed();
}

void SongEditor::selectSlide(const QString &key, bool extend)
{
    const auto [group, index] = fromSlideKey(key);
    const Group *g = m_song.group(group);
    if (!g || index < 0 || index >= g->slides.size())
        return;
    if (!extend)
        m_selected = {key};
    else if (!m_selected.removeOne(key))
        m_selected << key;
    m_warnings.clear();
    emit changed();
}

void SongEditor::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit changed();
}

void SongEditor::pruneSelection()
{
    m_selected.removeIf([this](const QString &key) {
        const auto [group, index] = fromSlideKey(key);
        const Group *g = m_song.group(group);
        return !g || index >= g->slides.size();
    });
}

QList<std::pair<Group *, int>> SongEditor::selectedSlides()
{
    QList<std::pair<Group *, int>> out;
    for (const QString &key : std::as_const(m_selected)) {
        const auto [group, index] = fromSlideKey(key);
        Group *g = m_song.group(group);
        if (g && index >= 0 && index < g->slides.size()) {
            g->fitSlideOverrides();
            out.append({g, index});
        }
    }
    return out;
}

QList<QJsonObject> SongEditor::selectedOverrides() const
{
    QList<QJsonObject> out;
    for (const QString &key : m_selected) {
        const auto [group, index] = fromSlideKey(key);
        if (const Group *g = m_song.group(group))
            out << g->slideOverride(index);
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

void SongEditor::setSlideProperty(const QString &path, const QVariant &value)
{
    const QList<std::pair<Group *, int>> slides = selectedSlides();
    if (!loaded() || slides.isEmpty())
        return;
    const Template t = songTemplate();
    const QJsonValue v = QJsonValue::fromVariant(value);
    QList<QJsonObject> edited;
    QStringList problems;
    for (const auto &[group, index] : slides) {
        const QJsonObject &was = group->slideOverrides[index];
        QJsonObject next = was;
        if (const QString why = overrides::setProperty(next, m_regionId, path, v); !why.isEmpty()) {
            problems << why;
            break;
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
        if (!problems.isEmpty())
            break;
        edited << next;
    }
    if (!problems.isEmpty()) {
        problems.removeDuplicates();
        m_warnings = problems;
        emit changed();   // a control showing the refused value goes back
        return;
    }
    for (int i = 0; i < slides.size(); ++i)
        slides[i].first->slideOverrides[slides[i].second] = edited[i];
    styleEdited();
}

void SongEditor::resetSlideProperty(const QString &path)
{
    for (const auto &[group, index] : selectedSlides())
        overrides::resetProperty(group->slideOverrides[index], m_regionId, path);
    styleEdited();
}

void SongEditor::resetSlides()
{
    for (const auto &[group, index] : selectedSlides())
        overrides::resetAll(group->slideOverrides[index]);
    styleEdited();
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
