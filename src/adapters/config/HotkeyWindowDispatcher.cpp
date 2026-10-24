#include "HotkeyWindowDispatcher.h"

#include <optional>
#include <utility>

#include <QEvent>
#include <QKeyEvent>

#include "HotkeyChord.h"
#include "HotkeyCatalogEntry.h"
#include "HotkeyManager.h"

HotkeyWindowDispatcher::HotkeyWindowDispatcher(HotkeyManager& hotkeyManager, QObject* parent)
    : QObject(parent)
    , m_hotkeyManager(hotkeyManager)
{
}

void HotkeyWindowDispatcher::registerCallback(HotkeyAction action, std::function<void()> callback)
{
    m_callbacks[action] = std::move(callback);
}

bool HotkeyWindowDispatcher::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::ShortcutOverride:
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (tryDispatch(keyEvent))
            {
                m_pendingChord = HotkeyChord::fromKeyEvent(keyEvent).sequence();
                return true;
            }
            break;
        }
        case QEvent::KeyPress:
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const QKeySequence chord = HotkeyChord::fromKeyEvent(keyEvent).sequence();
            if (!m_pendingChord.isEmpty() && chord == m_pendingChord)
            {
                // The unconditional KeyPress Qt sends right behind the ShortcutOverride we just
                // accepted for this same physical press -- already handled, swallow silently.
                m_pendingChord = QKeySequence();
                keyEvent->accept();
                return true;
            }

            if (tryDispatch(keyEvent))
            {
                return true;
            }
            break;
        }
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

bool HotkeyWindowDispatcher::tryDispatch(QKeyEvent* event)
{
    const QKeySequence chord = HotkeyChord::fromKeyEvent(event).sequence();
    const std::optional<HotkeyAction> action = m_hotkeyManager.resolve(HotkeyCatalogEntry::Scope::Window, chord);
    if (!action)
    {
        return false;
    }

    const auto it = m_callbacks.find(*action);
    if (it == m_callbacks.end())
    {
        return false;
    }

    event->accept();
    it->second();
    return true;
}
