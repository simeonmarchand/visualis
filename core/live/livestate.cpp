#include "livestate.h"

namespace visualis {

LiveState::LiveState(QObject *parent)
    : QObject(parent)
{
}

bool LiveState::acceptable(const LiveItem &item)
{
    if (item.error.isEmpty())
        return true;
    emit refused(QStringLiteral("\"%1\" can't be shown: %2.").arg(item.song.title, item.error));
    return false;
}

bool LiveState::show(const LiveItem &item, int index)
{
    if (!acceptable(item))
        return false;
    m_live = item;
    m_heldBack.clear();
    const bool valid = index >= 0 && index < m_live.slides.size();
    m_index = valid ? index : -1;
    enterSlide();
    emit changed();
    return valid;
}

bool LiveState::restore(const LiveItem &item, int index, bool textCleared, const BackgroundLayer &background,
                        const QUuid &service)
{
    if (!acceptable(item))
        return false;
    if (service != m_serviceId) {
        m_items.clear();
        m_serviceDefault = {};
    }
    m_serviceId = service;
    m_live = item;
    m_heldBack.clear();
    m_index = index >= 0 && index < m_live.slides.size() ? index : -1;
    m_textCleared = textCleared;
    // A media slide carries no text: what was held over before the crash isn't in the journal.
    if (const LiveSlide *s = currentLiveSlide(); !s || s->content)
        m_content = s ? *s->content : Content();
    applyBackground(background);
    emit changed();
    return true;
}

void LiveState::setService(const QUuid &id, const QList<LiveItem> &items, const QUuid &defaultBackground)
{
    m_serviceId = id;
    m_items = items;
    m_serviceDefault = defaultBackground;   // fills an empty layer on the next move, not now
    if (const int n = itemIndex(); n >= 0)
        replaceLive(m_items[n]);   // its song or template may have changed
    emit changed();
}

bool LiveState::update(const LiveItem &item)
{
    // A service item by its id, a library song by its song's.
    const bool live = m_live.source ? item.id() == m_live.id()
                                    : !item.source && !item.song.id.isNull() && item.song.id == m_live.song.id;
    if (!live)
        return false;
    replaceLive(item);
    emit changed();
    return true;
}

void LiveState::replaceLive(const LiveItem &item)
{
    if (!item.error.isEmpty() || (item.slides.isEmpty() && !m_live.slides.isEmpty())) {
        const QString why = item.error.isEmpty() ? QStringLiteral("its song is gone") : item.error;
        m_heldBack = QStringLiteral("\"%1\" can't be shown as it is now (%2), so the audience still sees it as it was.")
                         .arg(m_live.song.title, why);
        return;
    }
    m_live = item;
    m_heldBack.clear();
    if (m_index >= m_live.slides.size())
        m_index = int(m_live.slides.size()) - 1;
    // The text follows the new rendering; the background changes only on moves.
    if (const LiveSlide *s = currentLiveSlide(); !s || s->content)
        m_content = s ? *s->content : Content();
}

int LiveState::itemIndex() const
{
    if (!m_live.source)
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id() == m_live.id())
            return i;
    }
    return -1;
}

bool LiveState::goToItem(int n, int slide)
{
    if (n < 0 || n >= m_items.size())
        return false;
    const int count = int(m_items[n].slides.size());
    if (slide == -1)
        slide = count - 1;
    if (slide < 0 || slide >= count)
        return false;
    showItem(n, slide);
    return true;
}

void LiveState::showItem(int n, int slide)
{
    m_live = m_items[n];
    m_heldBack.clear();
    m_index = slide;
    enterSlide();
    emit changed();
}

bool LiveState::next()
{
    if (m_index + 1 < m_live.slides.size())
        return moveTo(m_index + 1);
    const int item = itemIndex();
    // Nothing live yet: Next starts the service.
    if (item < 0 && (m_live.source || !m_live.slides.isEmpty()))
        return false;
    for (int i = item + 1; i < m_items.size(); ++i) {
        if (!m_items[i].slides.isEmpty()) {
            showItem(i, 0);
            return true;
        }
    }
    return false;
}

bool LiveState::previous()
{
    if (m_index > 0)
        return moveTo(m_index - 1);
    for (int i = itemIndex() - 1; i >= 0; --i) {
        if (!m_items[i].slides.isEmpty()) {
            showItem(i, int(m_items[i].slides.size()) - 1);
            return true;
        }
    }
    return false;
}

bool LiveState::goToSlide(int n)
{
    return n >= 0 && n < m_live.slides.size() && moveTo(n);
}

bool LiveState::moveTo(int n)
{
    m_index = n;
    enterSlide();
    emit changed();
    return true;
}

void LiveState::enterSlide()
{
    const LiveSlide *s = currentLiveSlide();
    if (!s || s->content) {
        m_content = s ? *s->content : Content();
        m_textCleared = false;
    }
    if (!s)
        return;
    if (s->background)
        takeBackground(s->background->asset, s->background->transition);
    else if (m_background.asset.isNull() && !m_serviceDefault.isNull())
        takeBackground(m_serviceDefault, m_live.tmpl.transitions.background);
}

bool LiveState::goToGroupStartingWith(const QString &letter)
{
    if (letter.isEmpty())
        return false;
    // A group starts where its first slide is: indexInGroup 0. A repeated group starts
    // again at each step of the arrangement that uses it.
    QList<int> starts;
    for (int i = 0; i < m_live.slides.size(); ++i) {
        const SongSlide &s = m_live.slides[i].slide;
        if (s.indexInGroup == 0 && s.groupLabel.startsWith(letter, Qt::CaseInsensitive))
            starts.append(i);
    }
    if (starts.isEmpty())
        return false;
    for (int i : starts) {
        if (i > m_index)
            return moveTo(i);
    }
    return moveTo(starts.first());
}

void LiveState::showBackground()
{
    if (!m_background.cleared)
        return;
    m_background.cleared = false;
    emit changed();
}

MediaCheck LiveState::checkMedia(const QUuid &asset) const
{
    if (m_check)
        return m_check(asset);
    MediaCheck ok;
    ok.status = MediaStatus::Ok;
    return ok;
}

bool LiveState::applyBackground(const BackgroundLayer &layer)
{
    // A spun-down external drive can make this stat take seconds, holding up the operator
    // window and the outputs. Media lives in the library folder, normally on the internal disk.
    if (!layer.asset.isNull()) {
        if (const MediaCheck c = checkMedia(layer.asset); !c.ok()) {
            m_backgroundAlert = c.message;
            return false;
        }
    }
    m_background = layer;
    m_backgroundAlert.clear();
    return true;
}

bool LiveState::takeBackground(const QUuid &asset, const Transition &transition)
{
    if (asset == m_background.asset) {
        // Already live: no restart and no new transition, but a cleared one comes back.
        m_background.cleared = false;
        m_backgroundAlert.clear();
        return true;
    }
    return applyBackground({asset, false, transition});
}

bool LiveState::setBackground(const QUuid &asset, const Transition &transition)
{
    const BackgroundLayer before = m_background;
    const QString alertBefore = m_backgroundAlert;
    const bool shown = takeBackground(asset, transition);
    if (m_background != before || m_backgroundAlert != alertBefore)
        emit changed();
    return shown;
}

void LiveState::backgroundFailed(const QUuid &asset, const BackgroundLayer &showing, const QString &why)
{
    if (asset != m_background.asset)
        return;
    m_background = showing;
    m_backgroundAlert = why;
    emit changed();
}

void LiveState::restoreBackground(const BackgroundLayer &layer)
{
    applyBackground(layer);
    emit changed();
}

bool LiveState::pickBackground(const QString &assetId)
{
    return setBackground(QUuid::fromString(assetId), m_live.tmpl.transitions.background);
}

void LiveState::clearText()
{
    if (m_textCleared)
        return;
    m_textCleared = true;
    emit changed();
}

void LiveState::clearBackground()
{
    if (m_background.cleared)
        return;
    m_background.cleared = true;
    emit changed();
}

void LiveState::clearAll()
{
    if (m_textCleared && m_background.cleared)
        return;
    m_textCleared = true;
    m_background.cleared = true;
    emit changed();
}

const LiveSlide *LiveState::currentLiveSlide() const
{
    return m_index >= 0 && m_index < m_live.slides.size() ? &m_live.slides[m_index] : nullptr;
}

const SongSlide *LiveState::currentSlide() const
{
    const LiveSlide *s = currentLiveSlide();
    return s ? &s->slide : nullptr;
}

QString LiveState::currentText() const
{
    return m_content.regions.value(QStringLiteral("main")).text;
}

QString LiveState::nextText() const
{
    return m_index >= 0 && m_index + 1 < m_live.slides.size() ? m_live.slides[m_index + 1].slide.text : QString();
}

QString LiveState::groupLabel() const
{
    const SongSlide *s = currentSlide();
    return s ? s->groupLabel : QString();
}

QString LiveState::groupColor() const
{
    const SongSlide *s = currentSlide();
    return s ? s->groupColor : QString();
}

} // namespace visualis
