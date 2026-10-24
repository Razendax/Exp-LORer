#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "HotkeyChord.h"

// Pure C++ mutation API over hotkeys.json's persisted overrides (Architecture.md §14.x) -- same
// "plain mutation API, no I/O" posture as Favorites. Holds only *overrides*; an action absent from
// m_overrides simply uses its HotkeyCatalogEntry::defaultChord (see effectiveChord() below).
class HotkeyBindings
{
public:
    struct Override
    {
        HotkeyChord chord;

        // Monotonic counter bumped on every setBinding/clearBinding call (never on resetToDefault,
        // which erases the override rather than recording one) -- breaks same-scope duplicate-chord
        // ties by recency (spec §5's "most recently bound wins"), persisted so the ordering
        // survives a restart.
        std::uint64_t order = 0;
    };

    // `defaultChord` as-is if `stableId` has no override; an override with an empty chord is an
    // explicit Clear, returned as-is (an empty HotkeyChord) rather than falling back to the
    // default.
    HotkeyChord effectiveChord(const std::string& stableId, const HotkeyChord& defaultChord) const;

    void setBinding(const std::string& stableId, const HotkeyChord& chord);

    // Explicit Clear -- distinct from resetToDefault(): stores an override with an empty chord
    // rather than erasing any override at all.
    void clearBinding(const std::string& stableId);

    // Erases the override entirely, falling back to the catalog default.
    void resetToDefault(const std::string& stableId);

    void resetAll();

    const std::unordered_map<std::string, Override>& overrides() const noexcept { return m_overrides; }

    // Restores a previously-persisted override set verbatim (HotkeyStore::load), including
    // whatever `order` values and next-order counter were saved, so recency comparisons stay
    // correct across a restart. Unlike setBinding/clearBinding, does NOT recompute m_nextOrder from
    // the restored overrides -- the caller supplies the counter's resume value directly.
    void setOverrides(std::unordered_map<std::string, Override> overrides, std::uint64_t nextOrder);

    std::uint64_t nextOrder() const noexcept { return m_nextOrder; }

private:
    std::unordered_map<std::string, Override> m_overrides;
    std::uint64_t m_nextOrder = 1;
};
