#pragma once

#include <utility>

#include <QKeyEvent>
#include <QKeySequence>
#include <QString>

// Thin value type wrapping exactly one QKeySequence of exactly one key (Architecture.md §14.x) --
// v1 is one simple key-combo per action, no chorded/multi-key sequences (spec §6), so a HotkeyChord
// never needs to hold more than a single QKeySequence built from a single QKeyEvent.
class HotkeyChord
{
public:
    HotkeyChord() = default;
    explicit HotkeyChord(QKeySequence sequence)
        : m_sequence(std::move(sequence))
    {
    }

    static HotkeyChord fromKeyEvent(const QKeyEvent* event)
    {
        if (!event)
        {
            return HotkeyChord();
        }
        return HotkeyChord(QKeySequence(event->keyCombination()));
    }

    const QKeySequence& sequence() const noexcept { return m_sequence; }

    // An empty chord represents an explicit Clear (HotkeyBindings) or "no key event" (fromKeyEvent).
    bool isEmpty() const { return m_sequence.isEmpty(); }

    // True if this chord carries at least one Ctrl/Alt/Shift/Meta modifier -- backs the "needs a
    // modifier unless allowsBareRebind" rule (spec §4).
    bool hasModifier() const
    {
        if (m_sequence.isEmpty())
        {
            return false;
        }
        const Qt::KeyboardModifiers modifiers = m_sequence[0].keyboardModifiers();
        return modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::AltModifier)
            || modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::MetaModifier);
    }

    // Locale-appropriate text for the Settings page's rows -- native key names.
    QString toDisplayString() const { return m_sequence.toString(QKeySequence::NativeText); }

    // Locale-STABLE text for hotkeys.json -- PortableText round-trips identically regardless of the
    // system locale, unlike NativeText (Architecture.md §14.x).
    QString toStorageString() const { return m_sequence.toString(QKeySequence::PortableText); }

    // An empty `text` round-trips to an empty (Clear) chord rather than an error.
    static HotkeyChord fromStorageString(const QString& text)
    {
        if (text.isEmpty())
        {
            return HotkeyChord();
        }
        return HotkeyChord(QKeySequence(text, QKeySequence::PortableText));
    }

    friend bool operator==(const HotkeyChord& lhs, const HotkeyChord& rhs) { return lhs.m_sequence == rhs.m_sequence; }
    friend bool operator!=(const HotkeyChord& lhs, const HotkeyChord& rhs) { return !(lhs == rhs); }

private:
    QKeySequence m_sequence;
};
