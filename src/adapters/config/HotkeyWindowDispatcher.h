#pragma once

#include <functional>
#include <unordered_map>

#include <QKeySequence>
#include <QObject>

#include "HotkeyAction.h"

class HotkeyManager;
class QEvent;
class QKeyEvent;

// Window-scoped hotkey dispatch (Architecture.md §14.x, plan.md's "Two dispatch paths"):
// constructed once and installed as an event filter on the QApplication instance
// (qApp->installEventFilter), it fires a registered action's callback regardless of which widget
// inside the window currently has keyboard focus -- unlike FileBrowserView::handleKeyPress's
// view-scoped dispatch, which only acts while a file browser view itself has focus.
//
// Watches both QEvent::ShortcutOverride and QEvent::KeyPress, because neither alone is reliable
// for every widget:
//   - Qt sends ShortcutOverride to the focus widget just before (almost) every non-autorepeat
//     KeyPress, specifically so something can claim the chord for itself. Built-in widgets
//     (QLineEdit, QTextEdit, QAbstractItemView, ...) accept it for their own hardcoded standard-key
//     bindings (Ctrl+A, Ctrl+C, Delete, ...) *independently* of whether any QShortcut/QAction is
//     registered for them -- but accepting ShortcutOverride does not stop Qt from still delivering
//     the ordinary KeyPress afterward (it only skips Qt's shortcut-map lookup, which never matches
//     anything here anyway, since this app never calls QAction::setShortcut/QShortcut for the
//     actions this feature covers). So claiming the chord here, before the target widget's own
//     event() runs (filters installed on qApp always run first), stops a focused QLineEdit/
//     QTextEdit from quietly keeping the key for its own editing even when it wouldn't otherwise
//     have accepted it, and -- since we also accept() the event ourselves -- a KeyPress for the
//     very same physical press still follows right behind it unconditionally, so it must be
//     swallowed too without re-invoking the callback a second time (handled via m_pendingChord
//     below).
//   - Some surfaces (e.g. embedded QML content, which doesn't route through the normal
//     QWidgetWindow key-event path) never send ShortcutOverride at all -- only a plain KeyPress.
//     The KeyPress branch below is what catches those.
class HotkeyWindowDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyWindowDispatcher(HotkeyManager& hotkeyManager, QObject* parent = nullptr);

    // One callback per window-scoped HotkeyAction, registered once at startup (MainWindow's
    // constructor registers one per action it owns, same lambda-body-reuse posture as the existing
    // QAction::triggered connections for the same actions) -- re-registering the same action
    // replaces its callback.
    void registerCallback(HotkeyAction action, std::function<void()> callback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Resolves `event`'s chord against HotkeyManager (Scope::Window); if it matches a registered
    // callback, invokes it and accepts `event`. Returns true (caller should also return true from
    // eventFilter, to swallow the event) on a dispatch.
    bool tryDispatch(QKeyEvent* event);

    HotkeyManager& m_hotkeyManager;
    std::unordered_map<HotkeyAction, std::function<void()>> m_callbacks;

    // Set after a ShortcutOverride dispatch to the chord just handled; checked by the very next
    // KeyPress so the unconditional follow-up Qt sends for the same physical press is swallowed
    // without invoking the callback a second time. Cleared once consumed.
    QKeySequence m_pendingChord;
};
