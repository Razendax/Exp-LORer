#pragma once

// The three popup notification kinds (Architecture.md notification system): Info/Warning/Error,
// each with its own icon/color and independently configurable enabled/timeout settings
// (NotificationSettings) from Settings -> General -> Notifications.
enum class NotificationType
{
    Info,
    Warning,
    Error
};
