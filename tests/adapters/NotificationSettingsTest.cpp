#include <gtest/gtest.h>

#include "NotificationSettings.h"

TEST(NotificationSettings, DefaultConstructedHasAllTypesEnabledWithFiveSecondTimeout)
{
    NotificationSettings settings;

    for (NotificationType type : { NotificationType::Info, NotificationType::Warning, NotificationType::Error })
    {
        const NotificationTypeSettings typeSettings = settings.settingsFor(type);
        EXPECT_TRUE(typeSettings.enabled);
        EXPECT_EQ(typeSettings.timeoutMs, 5000);
    }
}

TEST(NotificationSettings, SetSettingsForOnlyAffectsTheGivenType)
{
    NotificationSettings settings;

    settings.setSettingsFor(NotificationType::Warning, false, 0);

    const NotificationTypeSettings warning = settings.settingsFor(NotificationType::Warning);
    EXPECT_FALSE(warning.enabled);
    EXPECT_EQ(warning.timeoutMs, 0);

    const NotificationTypeSettings info = settings.settingsFor(NotificationType::Info);
    EXPECT_TRUE(info.enabled);
    EXPECT_EQ(info.timeoutMs, 5000);

    const NotificationTypeSettings error = settings.settingsFor(NotificationType::Error);
    EXPECT_TRUE(error.enabled);
    EXPECT_EQ(error.timeoutMs, 5000);
}

TEST(NotificationSettings, SetSettingsForZeroTimeoutIsPreservedAsNeverAutoDismiss)
{
    NotificationSettings settings;

    settings.setSettingsFor(NotificationType::Error, true, 0);

    EXPECT_EQ(settings.settingsFor(NotificationType::Error).timeoutMs, 0);
}
