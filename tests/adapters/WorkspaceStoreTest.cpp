#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>

#include "AppConfigStore.h"
#include "WorkspaceStore.h"

namespace
{
    namespace fs = std::filesystem;

    class WorkspaceStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_workspace_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
    };

    bool hasWorkspace(const std::vector<WorkspaceEntry>& entries, const QString& id)
    {
        return std::any_of(entries.begin(), entries.end(), [&id](const WorkspaceEntry& entry) { return entry.id == id; });
    }
}

TEST_F(WorkspaceStoreTest, ListWorkspacesOnFreshInstallSelfHealsToSingleDefaultEntry)
{
    WorkspaceStore store(m_tempDir);

    const std::vector<WorkspaceEntry> entries = store.listWorkspaces();

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].id, QStringLiteral("default"));
    EXPECT_EQ(entries[0].name, QStringLiteral("Default"));
    EXPECT_TRUE(fs::exists(m_tempDir / "workspaces" / "index.json"));
}

TEST_F(WorkspaceStoreTest, ListWorkspacesMigratesLegacyConfigJsonIntoDefaultWorkspace)
{
    const fs::path legacyConfigPath = m_tempDir / "config.json";
    {
        AppConfig legacyConfig;
        legacyConfig.windowGeometry = QByteArray("legacy-geometry-bytes");
        AppConfigStore legacyStore(legacyConfigPath);
        ASSERT_TRUE(legacyStore.save(legacyConfig));
    }

    WorkspaceStore store(m_tempDir);
    const std::vector<WorkspaceEntry> entries = store.listWorkspaces();

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].id, QStringLiteral("default"));

    AppConfigStore migratedStore(store.configFilePath(entries[0].id));
    EXPECT_EQ(migratedStore.load().windowGeometry, QByteArray("legacy-geometry-bytes"));
}

TEST_F(WorkspaceStoreTest, CreateWorkspaceAddsANewEntryWithAFreshId)
{
    WorkspaceStore store(m_tempDir);

    const QString id = store.createWorkspace(QStringLiteral("My Workspace"));
    EXPECT_FALSE(id.isEmpty());

    const std::vector<WorkspaceEntry> entries = store.listWorkspaces();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_TRUE(hasWorkspace(entries, QStringLiteral("default")));
    EXPECT_TRUE(hasWorkspace(entries, id));
}

TEST_F(WorkspaceStoreTest, RenameWorkspaceChangesNameButKeepsIdStable)
{
    WorkspaceStore store(m_tempDir);
    const QString id = store.createWorkspace(QStringLiteral("Original Name"));

    ASSERT_TRUE(store.renameWorkspace(id, QStringLiteral("Renamed")));

    const std::vector<WorkspaceEntry> entries = store.listWorkspaces();
    const auto it = std::find_if(entries.begin(), entries.end(), [&id](const WorkspaceEntry& entry) { return entry.id == id; });
    ASSERT_NE(it, entries.end());
    EXPECT_EQ(it->name, QStringLiteral("Renamed"));
}

TEST_F(WorkspaceStoreTest, RenameWorkspaceOnUnknownIdReturnsFalse)
{
    WorkspaceStore store(m_tempDir);
    store.listWorkspaces();

    EXPECT_FALSE(store.renameWorkspace(QStringLiteral("not-a-real-id"), QStringLiteral("New Name")));
}

TEST_F(WorkspaceStoreTest, DeleteWorkspaceRemovesEntryAndItsConfigFile)
{
    WorkspaceStore store(m_tempDir);
    const QString id = store.createWorkspace(QStringLiteral("To Delete"));

    const fs::path configPath = store.configFilePath(id);
    AppConfigStore(configPath).save(AppConfig{});
    ASSERT_TRUE(fs::exists(configPath));

    EXPECT_TRUE(store.deleteWorkspace(id));

    const std::vector<WorkspaceEntry> entries = store.listWorkspaces();
    EXPECT_FALSE(hasWorkspace(entries, id));
    EXPECT_FALSE(fs::exists(configPath));
}

TEST_F(WorkspaceStoreTest, DeleteWorkspaceOnUnknownIdReturnsFalse)
{
    WorkspaceStore store(m_tempDir);
    store.listWorkspaces();

    EXPECT_FALSE(store.deleteWorkspace(QStringLiteral("not-a-real-id")));
}

TEST_F(WorkspaceStoreTest, DeletingEveryWorkspaceSelfHealsBackToDefaultOnNextList)
{
    WorkspaceStore store(m_tempDir);
    const std::vector<WorkspaceEntry> initial = store.listWorkspaces();
    ASSERT_EQ(initial.size(), 1u);

    ASSERT_TRUE(store.deleteWorkspace(initial[0].id));

    const std::vector<WorkspaceEntry> healed = store.listWorkspaces();
    ASSERT_EQ(healed.size(), 1u);
    EXPECT_EQ(healed[0].id, QStringLiteral("default"));
    EXPECT_EQ(healed[0].name, QStringLiteral("Default"));
}

TEST_F(WorkspaceStoreTest, ConfigFilePathIsPureAndRequiresNoExistingFile)
{
    WorkspaceStore store(m_tempDir);

    const fs::path path = store.configFilePath(QStringLiteral("some-id"));

    EXPECT_EQ(path, m_tempDir / "workspaces" / "some-id.json");
    EXPECT_FALSE(fs::exists(path));
}
