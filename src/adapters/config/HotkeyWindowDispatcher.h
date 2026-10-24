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
//     KeyPress, which this dispatcher -- being installed on qApp -- always sees before the target
//     widget's own event() runs. Dispatching here as well as on KeyPress means a Window-scoped
//     action still fires reliably even for a focus widget (QLineEdit, QTextEdit,
//     QAbstractItemView, ...) that would otherwise claim the chord for one of its own hardcoded
//     standard-key bindings first.
//   - Some surfaces (e.g. embedded QML content, which doesn't route through the normal
//     QWidgetWindow key-event path) never send ShortcutOverride at all -- only a plain KeyPress.
//     The KeyPress branch below is what catches those.
// Dispatching on both means the same physical keypress can resolve twice (ShortcutOverride, then
// the unconditional KeyPress Qt sends right behind it) -- m_pendingChord below suppresses the
// second invocation without suppressing the KeyPress event itself.
//
// The event is normally swallowed after a successful dispatch (same as always): letting it keep
// propagating unconditionally was tried and reverted -- confirmed by hand that once nothing along
// the ancestor chain actually wants an unrecognized chord like Ctrl+T, each widget in turn ignores
// it and Qt re-sends it to the next parent up, and since app-level filters see *every* one of
// those re-sends too, tryDispatch ends up invoked (and the action re-fired) once per ancestor
// instead of once. Two distinct cross-scope cases still need to deviate from plain swallowing, but
// neither risks that storm (HotkeyManager::conflictingAction is deliberately same-scope-only, spec
// §5, so neither shows up as a same-scope conflict either):
//   - A chord that ALSO resolves to a View-scoped action (tryDispatch's own check) defers to it
//     entirely -- the Window-scoped callback isn't even invoked, so there's nothing to swallow in
//     the first place. The one context where both could apply (a file browser view focused) is
//     exactly where the View-scoped action is meant to win outright, not merely share the keypress
//     -- confirmed via test_cross_scope_duplicate_chord_both_still_fire_independently.
//   - A chord that's live on a Window-scoped action while a Settings-page HotkeyEditWidget is
//     actively listening for a new one still dispatches the live action as always, but is left
//     unswallowed (recordingWidgetFocused() below) so the listening widget also gets this same
//     keystroke as its candidate chord. Safe to leave unswallowed specifically here (unlike the
//     general case above): HotkeyEditWidget::keyPressEvent always either handles the key itself or
//     returns without calling ignore(), so it's a dead end -- it never continues bubbling up to
//     re-trigger this same dispatch on some ancestor widget.
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
    // callback, invokes it and accepts `event`. Returns true on a dispatch -- eventFilter decides
    // from there whether to also swallow the event (see this header's own comment above).
    bool tryDispatch(QKeyEvent* event);

    HotkeyManager& m_hotkeyManager;
    std::unordered_map<HotkeyAction, std::function<void()>> m_callbacks;

    // Set after a ShortcutOverride dispatch to the chord just handled; checked by the very next
    // KeyPress so the unconditional follow-up Qt sends for the same physical press doesn't invoke
    // the callback a second time. Cleared once consumed.
    QKeySequence m_pendingChord;
};
