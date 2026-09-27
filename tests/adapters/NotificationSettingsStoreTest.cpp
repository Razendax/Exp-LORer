#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "NotificationSettingsStore.h"

namespace
{
    namespace fs = std::filesystem;

    class NotificationSettingsStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_notification_settings_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_settingsFilePath = m_tempDir / "notification_settings.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_settingsFilePath;
    };
}

TEST_F(NotificationSettingsStoreTest, LoadOnMissingFileReturnsBuiltInDefaults)
{
    NotificationSettingsStore store(m_settingsFilePath);

    const NotificationSettings settings = store.load();

    for (NotificationType type : { NotificationType::Info, NotificationType::Warning, NotificationType::Error })
    {
        const NotificationTypeSettings typeSettings = settings.settingsFor(type);
        EXPECT_TRUE(typeSettings.enabled);
        EXPECT_EQ(typeSettings.timeoutMs, 5000);
    }
}

TEST_F(NotificationSettingsStoreTest, SaveThenLoadRoundTripsEveryType)
{
    NotificationSettingsStore store(m_settingsFilePath);

    NotificationSettings settings;
    settings.setSettingsFor(NotificationType::Info, true, 3000);
    settings.setSettingsFor(NotificationType::Warning, false, 8000);
    settings.setSettingsFor(NotificationType::Error, true, 0);

    ASSERT_TRUE(store.save(settings));

    const NotificationSettings loaded = store.load();

    EXPECT_TRUE(loaded.settingsFor(NotificationType::Info).enabled);
    EXPECT_EQ(loaded.settingsFor(NotificationType::Info).timeoutMs, 3000);

    EXPECT_FALSE(loaded.settingsFor(NotificationType::Warning).enabled);
    EXPECT_EQ(loaded.settingsFor(NotificationType::Warning).timeoutMs, 8000);

    EXPECT_TRUE(loaded.settingsFor(NotificationType::Error).enabled);
    EXPECT_EQ(loaded.settingsFor(NotificationType::Error).timeoutMs, 0);
}

TEST_F(NotificationSettingsStoreTest, LoadOnMalformedJsonReturnsBuiltInDefaults)
{
    {
        std::ofstream file(m_settingsFilePath);
        file << "{ not valid json";
    }

    NotificationSettingsStore store(m_settingsFilePath);
    const NotificationSettings settings = store.load();

    EXPECT_TRUE(settings.settingsFor(NotificationType::Info).enabled);
    EXPECT_EQ(settings.settingsFor(NotificationType::Info).timeoutMs, 5000);
}

TEST_F(NotificationSettingsStoreTest, LoadIgnoresAnUnrecognizedTypeKeyAndKeepsDefaultsForKnownTypes)
{
    {
        std::ofstream file(m_settingsFilePath);
        file << R"({ "version": 1, "types": { "Bogus": { "enabled": false, "timeoutMs": 1 } } })";
    }

    NotificationSettingsStore store(m_settingsFilePath);
    const NotificationSettings settings = store.load();

    EXPECT_TRUE(settings.settingsFor(NotificationType::Info).enabled);
    EXPECT_EQ(settings.settingsFor(NotificationType::Info).timeoutMs, 5000);
}
