#include "library/songeditor.h"

#include "library/songlibrary.h"
#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songtext.h"

#include <QQmlEngine>

using namespace visualis;

namespace {
QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}
} // namespace

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
    const std::optional<Song> song = lib ? lib->loadSong(QUuid::fromString(id)) : std::nullopt;
    m_song = song.value_or(Song{});
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
    if (!lib || !lib->saveSong(m_song, &error))
        m_error = QStringLiteral("Couldn't save: %1").arg(error.isEmpty() ? QStringLiteral("no library") : error);
    else
        m_error.clear();
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
        m_arrangement = {};
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
    QVariantList list;
    for (const SongSlide &s : songSlides(m_song, m_arrangement))
        list.append(QVariantMap{{"text", s.text}, {"groupLabel", s.groupLabel}, {"groupColor", s.groupColor}});
    return list;
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
        m_song.defaultArrangement = {};
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
    m_song = Song{};
    m_arrangement = {};
    emit changed();
    emit lyricsReset();
    return true;
}
