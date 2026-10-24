#include "HotkeyWindowDispatcher.h"

#include <optional>
#include <utility>

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

#include "HotkeyChord.h"
#include "HotkeyCatalogEntry.h"
#include "HotkeyManager.h"

namespace
{
    // True while the currently-focused widget is a Settings-page HotkeyEditWidget actively
    // listening for a candidate chord. Checked via a plain dynamic property rather than
    // qobject_cast<HotkeyEditWidget*> -- HotkeyEditWidget lives in src/ui/widgets, which already
    // depends on this adapters/config target (for HotkeyManager itself), so a reverse #include
    // here would be circular. HotkeyEditWidget sets/clears this same "listening" property (also
    // used for its own QSS styling state) on enter/exitListeningMode(), so this stays in sync with
    // it for free.
    bool recordingWidgetFocused()
    {
        const QWidget* focusWidget = QApplication::focusWidget();
        return focusWidget && focusWidget->property("listening").toBool();
    }
}

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
                // Normally swallow (see tryDispatch's own comment for why), EXCEPT while a
                // Settings-page HotkeyEditWidget is actively listening for a new chord -- it needs
                // to see this same raw keystroke itself (spec §4's "warn but allow" promises a live
                // Window-scoped binding doesn't block recording a duplicate elsewhere). Safe to
                // leave unswallowed specifically here: HotkeyEditWidget::keyPressEvent always either
                // handles the key itself or returns without calling ignore(), so it never continues
                // bubbling up to re-trigger this same dispatch on some ancestor widget.
                if (!recordingWidgetFocused())
                {
                    return true;
                }
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
                // accepted for this same physical press -- already handled, don't invoke the
                // callback a second time. Swallow it too, same as the ShortcutOverride branch above
                // and for the same reason/exception.
                m_pendingChord = QKeySequence();
                if (!recordingWidgetFocused())
                {
                    keyEvent->accept();
                    return true;
                }
                break;
            }

            if (tryDispatch(keyEvent) && !recordingWidgetFocused())
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

    // A chord that ALSO resolves to a View-scoped action defers to it entirely -- confirmed via
    // test_cross_scope_duplicate_chord_both_still_fire_independently that the View-scoped action
    // (the more specific match, in the one context -- the file browser view having focus -- where
    // both could apply) is the one meant to win outright, not fire alongside the Window-scoped one.
    // Defaults never collide this way (spec §3), so this only ever applies to a chord the user has
    // deliberately duplicated across scopes.
    if (m_hotkeyManager.resolve(HotkeyCatalogEntry::Scope::View, chord))
    {
        return false;
    }

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
