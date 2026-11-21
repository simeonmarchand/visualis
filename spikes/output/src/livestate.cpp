#include "livestate.h"
#include "eventlog.h"

LiveState *LiveState::s_instance = nullptr;

LiveState::LiveState(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    // Public domain (John Newton, 1779), so it can live in the repo.
    m_slides = {
        "Amazing grace! How sweet the sound\nThat saved a wretch like me!",
        "I once was lost, but now am found;\nWas blind, but now I see.",
        "'Twas grace that taught my heart to fear,\nAnd grace my fears relieved;",
        "How precious did that grace appear\nThe hour I first believed!",
        "Through many dangers, toils and snares,\nI have already come;",
        "'Tis grace hath brought me safe thus far,\nAnd grace will lead me home.",
        "The Lord has promised good to me,\nHis word my hope secures;",
        "He will my shield and portion be\nAs long as life endures.",
        "When we've been there ten thousand years,\nBright shining as the sun,",
        "We've no less days to sing God's praise\nThan when we'd first begun.",
    };
}

LiveState *LiveState::create(QQmlEngine *, QJSEngine *engine)
{
    Q_ASSERT(s_instance && engine->thread() == s_instance->thread());
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString LiveState::currentText() const
{
    return m_textCleared ? QString() : m_slides.value(m_index);
}

QString LiveState::nextText() const
{
    return m_slides.value(m_index + 1);
}

void LiveState::next() { apply("next_slide", qMin(m_index + 1, int(m_slides.size()) - 1), false); }
void LiveState::prev() { apply("prev_slide", qMax(m_index - 1, 0), false); }
void LiveState::clearText() { apply("clear_text", m_index, true); }

void LiveState::goTo(int index)
{
    if (index >= 0 && index < m_slides.size())
        apply(QStringLiteral("goto_slide(%1)").arg(index), index, false);
}

void LiveState::apply(const QString &command, int index, bool cleared)
{
    EventLog::write(QStringLiteral("command %1").arg(command));
    if (index == m_index && cleared == m_textCleared)
        return;
    m_index = index;
    m_textCleared = cleared;
    emit changed();
}
