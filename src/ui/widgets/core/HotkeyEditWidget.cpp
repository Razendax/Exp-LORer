#include "HotkeyEditWidget.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QStyle>

#include "HotkeyChord.h"

HotkeyEditWidget::HotkeyEditWidget(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("hotkeyEditWidget"));
    setFocusPolicy(Qt::StrongFocus);
    refreshText();

    connect(this, &QPushButton::clicked, this, &HotkeyEditWidget::enterListeningMode);
}

void HotkeyEditWidget::setChord(const QKeySequence& chord)
{
    m_chord = chord;
    if (!m_listening)
    {
        refreshText();
    }
}

void HotkeyEditWidget::enterListeningMode()
{
    m_listening = true;
    setText(tr("Press a key combination..."));
    setProperty("listening", true);
    style()->unpolish(this);
    style()->polish(this);
    setFocus();
}

void HotkeyEditWidget::exitListeningMode()
{
    m_listening = false;
    setProperty("listening", false);
    style()->unpolish(this);
    style()->polish(this);
    refreshText();
}

void HotkeyEditWidget::refreshText()
{
    setText(m_chord.isEmpty() ? tr("(none)") : m_chord.toString(QKeySequence::NativeText));
}

void HotkeyEditWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_listening)
    {
        QPushButton::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        exitListeningMode();
        return;
    }

    // A bare modifier press by itself (Ctrl/Alt/Shift/Meta alone, with no other key yet) isn't a
    // candidate chord on its own -- wait for the full combination.
    switch (event->key())
    {
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Shift:
        case Qt::Key_Meta:
            return;
        default:
            break;
    }

    const HotkeyChord candidate = HotkeyChord::fromKeyEvent(event);
    if (!candidate.hasModifier() && !m_allowsBareRebind)
    {
        return; // Stays in listening mode -- rejected silently (spec §4).
    }

    exitListeningMode();
    emit chordAccepted(candidate.sequence());
}

void HotkeyEditWidget::focusOutEvent(QFocusEvent* event)
{
    if (m_listening)
    {
        exitListeningMode();
    }
    QPushButton::focusOutEvent(event);
}
