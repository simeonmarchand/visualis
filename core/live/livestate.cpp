#include "livestate.h"

namespace visualis {

LiveState::LiveState(QObject *parent)
    : QObject(parent)
{
}

bool LiveState::show(const Song &song, const QUuid &arrangement, int index)
{
    m_song = song;
    m_arrangement = arrangement;
    m_slides = songSlides(song, arrangement);
    const bool valid = index >= 0 && index < m_slides.size();
    m_index = valid ? index : -1;
    m_textCleared = false;
    emit changed();
    return valid;
}

void LiveState::restore(const Song &song, const QUuid &arrangement, int index, bool textCleared,
                        bool backgroundCleared)
{
    m_song = song;
    m_arrangement = arrangement;
    m_slides = songSlides(song, arrangement);
    m_index = index >= 0 && index < m_slides.size() ? index : -1;
    m_textCleared = textCleared;
    m_backgroundCleared = backgroundCleared;
    emit changed();
}

bool LiveState::next()
{
    return m_index + 1 < m_slides.size() && moveTo(m_index + 1);
}

bool LiveState::previous()
{
    return m_index > 0 && moveTo(m_index - 1);
}

bool LiveState::goToSlide(int n)
{
    return n >= 0 && n < m_slides.size() && moveTo(n);
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
    for (int i = 0; i < m_slides.size(); ++i) {
        const SongSlide &s = m_slides[i];
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
    return m_index >= 0 && m_index < m_slides.size() ? &m_slides[m_index] : nullptr;
}

QString LiveState::currentText() const
{
    const SongSlide *s = currentSlide();
    return s ? s->text : QString();
}

QString LiveState::nextText() const
{
    return m_index >= 0 && m_index + 1 < m_slides.size() ? m_slides[m_index + 1].text : QString();
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
