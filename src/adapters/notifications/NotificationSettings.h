#pragma once

#include <array>
#include <cstddef>

#include "NotificationType.h"

// Fixed {enabled, timeoutMs} entry for one NotificationType -- unlike FileDecorationRules'
// sparse override map, there are only ever exactly 3 rows (one per NotificationType), so no
// "unset" state is needed and a plain std::array indexed by type suffices.
struct NotificationTypeSettings
{
    bool enabled = true;
    // Milliseconds before a popup auto-dismisses. 0 means "never auto-dismiss, close button
    // only" -- kept as an explicit, allowed value rather than a special sentinel elsewhere.
    int timeoutMs = 5000;
};

// Persistent per-type popup settings, edited from Settings -> General -> Notifications and
// persisted via NotificationSettingsStore. Pure C++, no Qt -- same posture as HighlightTheme/
// FileDecorationRules.
class NotificationSettings
{
public:
    NotificationTypeSettings settingsFor(NotificationType type) const noexcept { return m_entries[indexOf(type)]; }
    void setSettingsFor(NotificationType type, bool enabled, int timeoutMs) noexcept
    {
        m_entries[indexOf(type)] = NotificationTypeSettings{ enabled, timeoutMs };
    }

private:
    static constexpr std::size_t indexOf(NotificationType type) noexcept { return static_cast<std::size_t>(type); }

    std::array<NotificationTypeSettings, 3> m_entries{};
};
