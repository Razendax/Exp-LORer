#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QKeySequence>
#include <QObject>

#include "AsyncSaveDispatcher.h"
#include "HotkeyBindings.h"
#include "HotkeyCatalogEntry.h"
#include "HotkeyStore.h"

// Qt-facing wrapper around HotkeyCatalog + HotkeyBindings + HotkeyStore (Architecture.md §14.x),
// constructed once in CompositionRoot and injected everywhere a chord needs to be read or rebound
// -- same "one shared live instance, mutation persists immediately via a background save,
// broadcasts a changed signal" posture as FavoritesViewModel. The actual HotkeyStore::save() call
// runs on a dedicated background thread (via m_saveDispatcher), so no mutation ever blocks the Qt
// UI thread on disk I/O.
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    HotkeyManager(HotkeyStore& store, const std::vector<HotkeyCatalogEntry>& catalog, QObject* parent = nullptr);

    const std::vector<HotkeyCatalogEntry>& catalog() const noexcept { return m_catalog; }

    QKeySequence chordFor(HotkeyAction action) const;

    // Returns false (no-op, no signal, nothing persisted) if `chord` has no modifier and the
    // action's catalog entry doesn't allowsBareRebind (spec §4) -- the Settings page's listening
    // widget already filters this itself before calling in, but HotkeyManager re-validates rather
    // than trusting the caller.
    bool setBinding(HotkeyAction action, const QKeySequence& chord);

    // Explicit Clear -- distinct from resetToDefault() below.
    void clearBinding(HotkeyAction action);

    void resetToDefault(HotkeyAction action);
    void resetAll();

    // Same-Scope lookup only -- cross-scope duplicates never contend (spec §5), so this never
    // reports a conflict against an action in a different Scope than `candidate`'s own. nullopt if
    // `candidate` is unknown, `chord` is empty, or nothing else in-scope currently resolves to it.
    std::optional<HotkeyAction> conflictingAction(HotkeyAction candidate, const QKeySequence& chord) const;

    // Among every action in `scope` currently bound (override or default) to this exact chord,
    // returns the one most recently bound (highest HotkeyBindings::Override::order) -- an action
    // still at its catalog default (no override) counts as order 0, so an explicitly-bound action
    // always outranks one left at default (spec §5's "most recently bound wins"). A tie (two
    // actions both still at default somehow sharing a chord) resolves to whichever the catalog
    // lists first -- unreachable as long as the catalog itself stays collision-free (spec §3).
    std::optional<HotkeyAction> resolve(HotkeyCatalogEntry::Scope scope, const QKeySequence& chord) const;

signals:
    void bindingsChanged();

private:
    const HotkeyCatalogEntry* entryFor(HotkeyAction action) const;
    void afterMutation();

    HotkeyStore& m_store;
    std::vector<HotkeyCatalogEntry> m_catalog;
    HotkeyBindings m_bindings;
    std::unordered_map<HotkeyAction, const HotkeyCatalogEntry*> m_entriesByAction;

    AsyncSaveDispatcher<HotkeyBindings> m_saveDispatcher;
};
