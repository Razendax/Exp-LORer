#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "EnvironmentVariablesStore.h"

namespace
{
    namespace fs = std::filesystem;

    class EnvironmentVariablesStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_environment_variables_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_filePath = m_tempDir / "environment_variables.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_filePath;
    };
}

TEST_F(EnvironmentVariablesStoreTest, LoadOnMissingFileReturnsEmptyList)
{
    EnvironmentVariablesStore store(m_filePath);

    const std::vector<EnvironmentVariable> variables = store.load();

    EXPECT_TRUE(variables.empty());
}

TEST_F(EnvironmentVariablesStoreTest, SaveThenLoadRoundTripsNameAndOrderedValues)
{
    EnvironmentVariablesStore store(m_filePath);

    std::vector<EnvironmentVariable> variables;
    EnvironmentVariable first;
    first.name = QStringLiteral("MY_PATH");
    first.values = { QStringLiteral("C:/a"), QStringLiteral("C:/b"), QStringLiteral("C:/a") };
    variables.push_back(first);

    EnvironmentVariable second;
    second.name = QStringLiteral("EMPTY_VAR");
    variables.push_back(second);

    ASSERT_TRUE(store.save(variables));

    const std::vector<EnvironmentVariable> loaded = store.load();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].name, QStringLiteral("MY_PATH"));
    ASSERT_EQ(loaded[0].values.size(), 3);
    EXPECT_EQ(loaded[0].values[0], QStringLiteral("C:/a"));
    EXPECT_EQ(loaded[0].values[1], QStringLiteral("C:/b"));
    EXPECT_EQ(loaded[0].values[2], QStringLiteral("C:/a"));
    EXPECT_EQ(loaded[1].name, QStringLiteral("EMPTY_VAR"));
    EXPECT_TRUE(loaded[1].values.empty());
}

TEST_F(EnvironmentVariablesStoreTest, LoadSkipsAnEntryMissingItsName)
{
    {
        std::ofstream file(m_filePath);
        file << R"({ "version": 1, "variables": [ { "values": ["x"] }, { "name": "KEPT", "values": [] } ] })";
    }

    EnvironmentVariablesStore store(m_filePath);
    const std::vector<EnvironmentVariable> variables = store.load();

    ASSERT_EQ(variables.size(), 1u);
    EXPECT_EQ(variables[0].name, QStringLiteral("KEPT"));
}

TEST_F(EnvironmentVariablesStoreTest, LoadOnMalformedJsonReturnsEmptyList)
{
    {
        std::ofstream file(m_filePath);
        file << "{ not valid json";
    }

    EnvironmentVariablesStore store(m_filePath);
    const std::vector<EnvironmentVariable> variables = store.load();

    EXPECT_TRUE(variables.empty());
}
