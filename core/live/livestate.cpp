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
    const bool valid = index >= 0 && index < m_live.slides.size();
    m_index = valid ? index : -1;
    m_textCleared = false;
    emit changed();
    return valid;
}

bool LiveState::restore(const LiveItem &item, int index, bool textCleared, bool backgroundCleared,
                        const QUuid &service)
{
    if (!acceptable(item))
        return false;
    if (service != m_serviceId)
        m_items.clear();
    m_serviceId = service;
    m_live = item;
    m_index = index >= 0 && index < m_live.slides.size() ? index : -1;
    m_textCleared = textCleared;
    m_backgroundCleared = backgroundCleared;
    emit changed();
    return true;
}

void LiveState::setService(const QUuid &id, const QList<LiveItem> &items)
{
    m_serviceId = id;
    m_items = items;
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
    m_live = item;
    if (m_index >= m_live.slides.size())
        m_index = int(m_live.slides.size()) - 1;
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
    m_index = slide;
    m_textCleared = false;
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
    m_textCleared = false;
    emit changed();
    return true;
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
    if (!m_backgroundCleared)
        return;
    m_backgroundCleared = false;
    emit changed();
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
    if (m_backgroundCleared)
        return;
    m_backgroundCleared = true;
    emit changed();
}

void LiveState::clearAll()
{
    if (m_textCleared && m_backgroundCleared)
        return;
    m_textCleared = true;
    m_backgroundCleared = true;
    emit changed();
}

const SongSlide *LiveState::currentSlide() const
{
    return m_index >= 0 && m_index < m_live.slides.size() ? &m_live.slides[m_index].slide : nullptr;
}

Content LiveState::content() const
{
    return m_index >= 0 && m_index < m_live.slides.size() ? m_live.slides[m_index].content : Content();
}

QString LiveState::currentText() const
{
    const SongSlide *s = currentSlide();
    return s ? s->text : QString();
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
