#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "HotkeyCatalog.h"
#include "HotkeyStore.h"

namespace
{
    namespace fs = std::filesystem;

    class HotkeyStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_hotkey_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_hotkeysFilePath = m_tempDir / "hotkeys.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_hotkeysFilePath;
    };

    // A real, known stableId from the Milestone 1 catalog -- used so "unknown id" tests have a
    // genuinely-known id to contrast against.
    const std::string& knownStableId()
    {
        return HotkeyCatalog::entries().front().stableId;
    }
}

TEST_F(HotkeyStoreTest, LoadOnMissingFileReturnsEmptyBindings)
{
    HotkeyStore store(m_hotkeysFilePath);

    const HotkeyBindings bindings = store.load();

    EXPECT_TRUE(bindings.overrides().empty());
}

TEST_F(HotkeyStoreTest, SaveThenLoadRoundTripsAnOverrideWithItsOrder)
{
    HotkeyStore store(m_hotkeysFilePath);

    HotkeyBindings bindings;
    bindings.setBinding(knownStableId(), HotkeyChord(QKeySequence(QStringLiteral("Ctrl+Shift+C"), QKeySequence::PortableText)));

    ASSERT_TRUE(store.save(bindings));

    const HotkeyBindings loaded = store.load();
    ASSERT_EQ(loaded.overrides().count(knownStableId()), 1u);
    EXPECT_EQ(loaded.overrides().at(knownStableId()).chord.toStorageString(), QStringLiteral("Ctrl+Shift+C"));
    EXPECT_EQ(loaded.overrides().at(knownStableId()).order, bindings.overrides().at(knownStableId()).order);
}

TEST_F(HotkeyStoreTest, SaveThenLoadRoundTripsAnEmptyChordAsAnExplicitClear)
{
    HotkeyStore store(m_hotkeysFilePath);

    HotkeyBindings bindings;
    bindings.clearBinding(knownStableId());

    ASSERT_TRUE(store.save(bindings));

    const HotkeyBindings loaded = store.load();
    ASSERT_EQ(loaded.overrides().count(knownStableId()), 1u);
    EXPECT_TRUE(loaded.overrides().at(knownStableId()).chord.isEmpty());
}

TEST_F(HotkeyStoreTest, LoadSkipsAnUnknownStableId)
{
    {
        std::ofstream file(m_hotkeysFilePath);
        file << R"({ "version": 1, "nextOrder": 2, "overrides": [
                 { "id": "no.such.action", "chord": "Ctrl+Shift+Z", "order": 1 }
             ] })";
    }

    HotkeyStore store(m_hotkeysFilePath);
    const HotkeyBindings bindings = store.load();

    EXPECT_TRUE(bindings.overrides().empty());
}

TEST_F(HotkeyStoreTest, LoadSkipsAnEntryMissingItsId)
{
    {
        std::ofstream file(m_hotkeysFilePath);
        file << R"({ "version": 1, "nextOrder": 2, "overrides": [
                 { "chord": "Ctrl+Shift+Z", "order": 1 }
             ] })";
    }

    HotkeyStore store(m_hotkeysFilePath);
    const HotkeyBindings bindings = store.load();

    EXPECT_TRUE(bindings.overrides().empty());
}

TEST_F(HotkeyStoreTest, LoadOnMalformedJsonReturnsEmptyBindings)
{
    {
        std::ofstream file(m_hotkeysFilePath);
        file << "{ not valid json";
    }

    HotkeyStore store(m_hotkeysFilePath);
    const HotkeyBindings bindings = store.load();

    EXPECT_TRUE(bindings.overrides().empty());
}
