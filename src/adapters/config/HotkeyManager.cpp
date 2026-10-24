#include "HotkeyManager.h"

HotkeyManager::HotkeyManager(HotkeyStore& store, const std::vector<HotkeyCatalogEntry>& catalog, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_catalog(catalog)
    , m_bindings(store.load())
    , m_saveDispatcher([&store](const HotkeyBindings& bindings) { store.save(bindings); })
{
    for (const HotkeyCatalogEntry& entry : m_catalog)
    {
        m_entriesByAction[entry.action] = &entry;
    }
}

QKeySequence HotkeyManager::chordFor(HotkeyAction action) const
{
    const HotkeyCatalogEntry* entry = entryFor(action);
    if (!entry)
    {
        return QKeySequence();
    }
    return m_bindings.effectiveChord(entry->stableId, entry->defaultChord).sequence();
}

bool HotkeyManager::setBinding(HotkeyAction action, const QKeySequence& chord)
{
    const HotkeyCatalogEntry* entry = entryFor(action);
    if (!entry)
    {
        return false;
    }

    const HotkeyChord candidate(chord);
    if (!candidate.isEmpty() && !candidate.hasModifier() && !entry->allowsBareRebind)
    {
        return false;
    }

    m_bindings.setBinding(entry->stableId, candidate);
    afterMutation();
    return true;
}

void HotkeyManager::clearBinding(HotkeyAction action)
{
    const HotkeyCatalogEntry* entry = entryFor(action);
    if (!entry)
    {
        return;
    }
    m_bindings.clearBinding(entry->stableId);
    afterMutation();
}

void HotkeyManager::resetToDefault(HotkeyAction action)
{
    const HotkeyCatalogEntry* entry = entryFor(action);
    if (!entry)
    {
        return;
    }
    m_bindings.resetToDefault(entry->stableId);
    afterMutation();
}

void HotkeyManager::resetAll()
{
    m_bindings.resetAll();
    afterMutation();
}

std::optional<HotkeyAction> HotkeyManager::conflictingAction(HotkeyAction candidate, const QKeySequence& chord) const
{
    const HotkeyCatalogEntry* candidateEntry = entryFor(candidate);
    if (!candidateEntry || chord.isEmpty())
    {
        return std::nullopt;
    }

    for (const HotkeyCatalogEntry& entry : m_catalog)
    {
        if (entry.action == candidate || entry.scope != candidateEntry->scope)
        {
            continue;
        }
        if (chordFor(entry.action) == chord)
        {
            return entry.action;
        }
    }
    return std::nullopt;
}

std::optional<HotkeyAction> HotkeyManager::resolve(HotkeyCatalogEntry::Scope scope, const QKeySequence& chord) const
{
    if (chord.isEmpty())
    {
        return std::nullopt;
    }

    std::optional<HotkeyAction> best;
    std::uint64_t bestOrder = 0;

    const auto& overrides = m_bindings.overrides();

    for (const HotkeyCatalogEntry& entry : m_catalog)
    {
        if (entry.scope != scope || chordFor(entry.action) != chord)
        {
            continue;
        }

        const auto it = overrides.find(entry.stableId);
        const std::uint64_t order = (it != overrides.end()) ? it->second.order : 0;

        if (!best || order > bestOrder)
        {
            best = entry.action;
            bestOrder = order;
        }
    }
    return best;
}

const HotkeyCatalogEntry* HotkeyManager::entryFor(HotkeyAction action) const
{
    const auto it = m_entriesByAction.find(action);
    return it != m_entriesByAction.end() ? it->second : nullptr;
}

void HotkeyManager::afterMutation()
{
    emit bindingsChanged();
    m_saveDispatcher.dispatch(m_bindings);
}
