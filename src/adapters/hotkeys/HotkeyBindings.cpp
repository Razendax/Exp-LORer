#include "HotkeyBindings.h"

#include <utility>

HotkeyChord HotkeyBindings::effectiveChord(const std::string& stableId, const HotkeyChord& defaultChord) const
{
    const auto it = m_overrides.find(stableId);
    if (it == m_overrides.end())
    {
        return defaultChord;
    }
    return it->second.chord;
}

void HotkeyBindings::setBinding(const std::string& stableId, const HotkeyChord& chord)
{
    m_overrides[stableId] = Override{ chord, m_nextOrder++ };
}

void HotkeyBindings::clearBinding(const std::string& stableId)
{
    m_overrides[stableId] = Override{ HotkeyChord(), m_nextOrder++ };
}

void HotkeyBindings::resetToDefault(const std::string& stableId)
{
    m_overrides.erase(stableId);
}

void HotkeyBindings::resetAll()
{
    m_overrides.clear();
}

void HotkeyBindings::setOverrides(std::unordered_map<std::string, Override> overrides, std::uint64_t nextOrder)
{
    m_overrides = std::move(overrides);
    m_nextOrder = nextOrder;
}
