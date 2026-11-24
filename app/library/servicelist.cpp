#include "library/servicelist.h"

#include "library/library.h"
#include "library/songlibrary.h"
#include "live/livestate.h"
#include "pco/songlink.h"
#include "service/servicejson.h"
#include "service/servicestore.h"
#include "song/songmatch.h"
#include "song/songstore.h"
#include "template/overrides.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QQmlEngine>

using namespace visualis;

Q_LOGGING_CATEGORY(lcServices, "visualis.services")

namespace {
QString braceless(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
constexpr auto kLastServiceSetting = "operator.lastService";
constexpr auto kServiceSuffix = ".visualis-service.json";
} // namespace

ServiceList::ServiceList(SongLibrary *songs, LiveState *live, QObject *parent)
    : QObject(parent)
    , m_songs(songs)
    , m_live(live)
{
    if (m_songs) {
        connect(m_songs, &SongLibrary::libraryChanged, this, &ServiceList::libraryChanged);
        connect(m_songs, &SongLibrary::templatesChanged, this, &ServiceList::refresh);
        // A song edited or deleted changes the items that use it.
        connect(m_songs, &SongLibrary::songsChanged, this, [this] {
            if (m_open)
                refresh();
        });
    }
    if (m_live)
        connect(m_live, &LiveState::changed, this, &ServiceList::followLive);
    libraryChanged();
}

ServiceList::~ServiceList() = default;

ServiceList *ServiceList::create(QQmlEngine *qml, QJSEngine *)
{
    return new ServiceList(qml->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary"),
                           qml->singletonInstance<LiveState *>("Visualis.Ui", "LiveState"));
}

void ServiceList::libraryChanged()
{
    m_open.reset();
    m_liveItems.clear();
    m_selected = -1;
    m_inGrid = false;
    Library *library = m_songs ? m_songs->library() : nullptr;
    m_store = library ? std::make_unique<ServiceStore>(*library) : nullptr;
    emit servicesChanged();
    // Back to the service the operator had open last time.
    if (!library || !openService(library->setting(kLastServiceSetting))) {
        emit serviceChanged();
        emit selectionChanged();
    }
}

QVariantList ServiceList::services() const
{
    QVariantList list;
    if (!m_store)
        return list;
    for (const ServiceSummary &s : m_store->list()) {
        list.append(QVariantMap{{"id", braceless(s.id)}, {"title", s.title},
                                {"date", s.date.isValid() ? s.date.toString("d MMM yyyy") : QString()}});
    }
    return list;
}

bool ServiceList::createService(const QString &title)
{
    if (!m_store)
        return false;
    Service s;
    s.title = title.trimmed();
    s.date = QDate::currentDate();
    if (const ServiceStore::Result r = m_store->save(s); !r.ok) {
        setReport(r.error);
        return false;
    }
    emit servicesChanged();
    return openService(braceless(s.id));
}

bool ServiceList::openService(const QString &id)
{
    if (!m_store || id.isEmpty())
        return false;
    std::optional<Service> s = m_store->load(QUuid::fromString(id));
    if (!s)
        return false;
    m_open = std::move(s);
    m_selected = -1;
    m_inGrid = false;
    m_songs->library()->setSetting(kLastServiceSetting, braceless(m_open->id));
    refresh();
    // Opening the service that's live (a recovery) shows the live item.
    if (m_live && m_live->itemIndex() >= 0)
        select(m_live->itemIndex());
    return true;
}

bool ServiceList::openAdded(const QString &id)
{
    emit servicesChanged();
    return openService(id);
}

bool ServiceList::renameService(const QString &title)
{
    if (!m_open || title.trimmed().isEmpty())
        return false;
    const QString before = m_open->title;
    m_open->title = title.trimmed();
    if (!save()) {
        m_open->title = before;
        return false;
    }
    emit servicesChanged();
    emit serviceChanged();
    return true;
}

bool ServiceList::removeService(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (!m_store || !m_store->remove(uuid))
        return false;
    if (m_open && m_open->id == uuid) {
        m_open.reset();
        m_liveItems.clear();
        m_selected = -1;
        m_inGrid = false;
        if (m_live && m_live->serviceId() == uuid)
            m_live->clearService();
        emit serviceChanged();
        emit selectionChanged();
    }
    emit servicesChanged();
    return true;
}

bool ServiceList::addSong(const QString &songId)
{
    const std::optional<Song> song = m_songs ? m_songs->loadSong(QUuid::fromString(songId)) : std::nullopt;
    if (!song)
        return false;
    ServiceItem item;
    item.kind = ServiceItem::Kind::Song;
    item.title = song->title;
    item.ref = song->id;
    return insert(item);
}

bool ServiceList::addHeader(const QString &title)
{
    if (title.trimmed().isEmpty())
        return false;
    ServiceItem item;
    item.kind = ServiceItem::Kind::Header;
    item.title = title.trimmed();
    return insert(item);
}

bool ServiceList::addText(const QString &title, const QString &body)
{
    ServiceItem item;
    item.kind = ServiceItem::Kind::Text;
    item.title = title.trimmed().isEmpty() ? (body.trimmed().isEmpty() ? QStringLiteral("Blank") : QStringLiteral("Text"))
                                           : title.trimmed();
    item.body = body.trimmed();
    return insert(item);
}

bool ServiceList::linkSong(int index, const QString &songId)
{
    if (!m_open || !m_songs || index < 0 || index >= m_open->items.size())
        return false;
    // A copy: saving replaces the service's item list, so no reference into it survives save().
    const ServiceItem before = m_open->items[index];
    if (before.kind != ServiceItem::Kind::Placeholder && before.kind != ServiceItem::Kind::Song)
        return false;
    std::optional<Song> song = m_songs->loadSong(QUuid::fromString(songId));
    if (!song)
        return false;
    if (const std::optional<PcoSong> pco = snapshotSong(before.importSnapshot)) {
        // The Planning Center id moves to the chosen song, so a wrong link (by title, last
        // week) doesn't stick, and next week matches this one at once.
        QString error;
        for (const SongCandidate &c : SongStore(*m_songs->library()).matchCandidates()) {
            if (c.id == song->id || c.pcoSongId != pco->id)
                continue;
            if (std::optional<Song> holder = m_songs->loadSong(c.id); holder && releasePcoSong(*holder, pco->id))
                m_songs->saveSong(*holder, &error);
        }
        if (takePcoSong(*song, *pco))
            m_songs->saveSong(*song, &error);
        if (!error.isEmpty())
            setReport(error);   // still linked for this service; it just won't match by id next week
    }
    ServiceItem linked = linkedItem(before, *song);
    linked.title = song->title;
    const QStringList missing = unmatchedLabels(linked, *song);
    m_open->items[index] = linked;
    if (!save()) {
        m_open->items[index] = before;
        return false;
    }
    if (!missing.isEmpty())
        setReport(tr("No group for %1 in \"%2\".").arg(missing.join(QStringLiteral(", ")), song->title));
    refresh();
    select(index);
    return true;
}

bool ServiceList::leaveBlank(int index)
{
    if (!m_open || index < 0 || index >= m_open->items.size())
        return false;
    const ServiceItem before = m_open->items[index];
    ServiceItem blank = before;
    blank.kind = ServiceItem::Kind::Text;   // an empty text item is one blank slide
    blank.ref = QUuid();
    blank.arrangement = QUuid();
    blank.body.clear();
    m_open->items[index] = blank;
    if (!save()) {
        m_open->items[index] = before;
        return false;
    }
    refresh();
    select(index);
    return true;
}

bool ServiceList::setItemTemplate(int index, const QString &templateId)
{
    if (!m_open || index < 0 || index >= m_open->items.size())
        return false;
    const QUuid itemId = m_open->items[index].id;
    const QUuid chosen = QUuid::fromString(templateId);
    Service saved;
    const ServiceStore::Result r = m_store->applyWithoutWaiting(
        m_open->id,
        [&](const Service &current) -> std::optional<ServiceStore::Decision> {
            Service s = current;
            for (ServiceItem &item : s.items) {
                if (item.id != itemId)
                    continue;
                if (chosen.isNull())
                    overrides::resetTemplate(item.overrides);
                else
                    overrides::setTemplate(item.overrides, chosen);
                return ServiceStore::Decision{s, {}};
            }
            return std::nullopt;
        },
        &saved);
    if (!r.ok) {
        setReport(r.busy ? tr("The template wasn't changed: the library is in use by another program. Try again.") : r.error);
        return false;
    }
    m_open = saved;
    refresh();
    return true;
}

bool ServiceList::insert(ServiceItem item)
{
    if (!m_open)
        return false;
    const int at = m_selected >= 0 ? m_selected + 1 : int(m_open->items.size());
    m_open->items.insert(at, item);
    if (!save()) {
        m_open->items.removeAt(at);
        return false;
    }
    refresh();
    select(at);
    return true;
}

bool ServiceList::removeItem(int index)
{
    if (!m_open || index < 0 || index >= m_open->items.size())
        return false;
    const ServiceItem removed = m_open->items.takeAt(index);
    if (!save()) {
        m_open->items.insert(index, removed);
        return false;
    }
    if (m_selected == index)
        m_selected = -1;
    else if (m_selected > index)
        --m_selected;
    refresh();
    emit selectionChanged();
    return true;
}

bool ServiceList::moveItem(int from, int to)
{
    if (!m_open)
        return false;
    const int n = int(m_open->items.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    m_open->items.move(from, to);
    if (!save()) {
        m_open->items.move(to, from);
        return false;
    }
    if (m_selected == from)
        m_selected = to;
    else if (from < m_selected && m_selected <= to)
        --m_selected;
    else if (to <= m_selected && m_selected < from)
        ++m_selected;
    refresh();
    emit selectionChanged();
    return true;
}

void ServiceList::select(int index)
{
    m_selected = m_open && index >= 0 && index < m_open->items.size() ? index : -1;
    m_inGrid = m_selected >= 0;
    emit selectionChanged();
}

void ServiceList::showLibrarySong()
{
    if (!m_inGrid)
        return;
    m_inGrid = false;
    emit selectionChanged();
}

bool ServiceList::save()
{
    const ServiceStore::Result r = m_store->save(*m_open);
    if (!r.ok)
        setReport(r.error);
    return r.ok;
}

void ServiceList::refresh()
{
    m_liveItems.clear();
    m_suggestions.clear();
    QHash<QUuid, QStringList> logged;   // only the open items', so the map doesn't grow
    if (m_open) {
        QList<SongCandidate> candidates;
        bool haveCandidates = false;
        const TemplateSet templates = m_songs ? m_songs->templates() : templateSet({});
        for (const ServiceItem &item : std::as_const(m_open->items)) {
            const bool isSong = item.kind == ServiceItem::Kind::Song;
            m_liveItems << liveItem(item, isSong && m_songs ? m_songs->loadSong(item.ref) : std::nullopt, templates);
            logged[item.id] = logNewWarnings(item, m_liveItems.last().warnings);
            // What an unlinked imported song may be, for the operator to pick (#55).
            QVariantList suggestions;
            const std::optional<PcoSong> pco = snapshotSong(item.importSnapshot);
            if (item.kind == ServiceItem::Kind::Placeholder && pco && m_songs && m_songs->library()) {
                if (!haveCandidates) {
                    candidates = SongStore(*m_songs->library()).matchCandidates();
                    haveCandidates = true;
                }
                const SongMatch m = matchSong(*pco, candidates);
                QList<QUuid> ids = m.kind == SongMatch::Kind::Linked ? QList<QUuid>{m.songId} : m.candidates;
                for (const QUuid &id : ids) {
                    for (const SongCandidate &c : std::as_const(candidates)) {
                        if (c.id == id)
                            suggestions << QVariantMap{{"id", braceless(c.id)}, {"title", c.title}};
                    }
                }
            }
            m_suggestions << suggestions;
        }
    }
    m_loggedWarnings = logged;
    if (m_live)
        m_live->setService(m_open ? m_open->id : QUuid(), m_liveItems);
    emit serviceChanged();
    emit selectionChanged();
}

QStringList ServiceList::logNewWarnings(const ServiceItem &item, const QStringList &warnings) const
{
    // Each logged once, not on every refresh.
    for (const QString &w : warnings) {
        if (!m_loggedWarnings.value(item.id).contains(w))
            qCWarning(lcServices).noquote() << QStringLiteral("\"%1\":").arg(item.title) << w;
    }
    return warnings;
}

void ServiceList::followLive()
{
    // When what's live moves to another item (Next past an item's end), the grid follows,
    // so the operator sees the slides that are on screen.
    const QUuid item = m_live->itemId();
    if (item == m_lastLiveItem)
        return;
    m_lastLiveItem = item;
    if (const int index = m_live->itemIndex(); index >= 0 && m_open && m_live->serviceId() == m_open->id)
        select(index);
}

void ServiceList::importServices(const QUrl &folder)
{
    if (!m_store)
        return;
    int imported = 0;
    QStringList lines;
    QString last;
    const QFileInfoList files = QDir(folder.toLocalFile())
                                    .entryInfoList({QStringLiteral("*") + kServiceSuffix}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        QFile f(file.absoluteFilePath());
        // A service is a few KB; anything far bigger isn't one.
        if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) {
            lines << QStringLiteral("Couldn't read %1: %2").arg(file.fileName(), f.isOpen() ? "too large" : f.errorString());
            continue;
        }
        ServiceJsonResult r = serviceFromJson(f.readAll());
        if (!r.service) {
            lines << QStringLiteral("Couldn't read %1: %2").arg(file.fileName(), r.error);
            continue;
        }
        if (!r.service->id.isNull() && m_store->load(r.service->id)) {
            lines << QStringLiteral("Skipped %1: already in the library.").arg(file.fileName());
            continue;
        }
        if (const ServiceStore::Result saved = m_store->save(*r.service); !saved.ok) {
            lines << QStringLiteral("Couldn't save %1: %2").arg(file.fileName(), saved.error);
            continue;
        }
        for (const QString &w : std::as_const(r.warnings))
            lines << QStringLiteral("%1: %2").arg(file.fileName(), w);
        ++imported;
        last = braceless(r.service->id);
    }
    lines.prepend(QStringLiteral("Imported %1 services.").arg(imported));
    setReport(lines.join('\n'));
    emit servicesChanged();
    if (!last.isEmpty())
        openService(last);
}

QString ServiceList::openId() const
{
    return m_open ? braceless(m_open->id) : QString();
}

QString ServiceList::openTitle() const
{
    return m_open ? m_open->title : QString();
}

QVariantList ServiceList::items() const
{
    QVariantList list;
    if (!m_open)
        return list;
    for (int i = 0; i < m_open->items.size(); ++i) {
        const ServiceItem &it = m_open->items[i];
        const bool songPlaceholder = it.kind == ServiceItem::Kind::Placeholder && snapshotSong(it.importSnapshot);
        list.append(QVariantMap{{"id", braceless(it.id)}, {"kind", kindName(it.kind)}, {"title", it.title},
                                {"hasSlides", !m_liveItems.value(i).slides.isEmpty()},
                                {"warnings", m_liveItems.value(i).warnings.join('\n')},
                                {"songPlaceholder", songPlaceholder}, {"suggestions", m_suggestions.value(i)},
                                {"pcoRemoved", it.pcoRemoved},
                                {"templateId", braceless(overrideTemplateId(it.overrides))}});
    }
    return list;
}

QString ServiceList::selectedTitle() const
{
    return m_open && m_selected >= 0 ? m_open->items[m_selected].title : QString();
}

QString ServiceList::selectedSongId() const
{
    if (!m_open || m_selected < 0)
        return {};
    const ServiceItem &it = m_open->items[m_selected];
    return it.kind == ServiceItem::Kind::Song ? braceless(it.ref) : QString();
}

QVariantList ServiceList::selectedSlides() const
{
    if (m_selected < 0 || m_selected >= m_liveItems.size())
        return {};
    return slideMaps(m_liveItems[m_selected].slides);
}

void ServiceList::setReport(const QString &report)
{
    m_report = report;
    qCInfo(lcServices).noquote() << report;
    emit reportChanged();
}
