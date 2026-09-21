#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "FileDecorationRulesStore.h"

namespace
{
    namespace fs = std::filesystem;

    class FileDecorationRulesStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_file_decoration_rules_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_rulesFilePath = m_tempDir / "file_decorations.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_rulesFilePath;
    };
}

TEST_F(FileDecorationRulesStoreTest, LoadOnMissingFileReturnsEmptyRuleList)
{
    FileDecorationRulesStore store(m_rulesFilePath);

    const FileDecorationRules rules = store.load();

    EXPECT_TRUE(rules.rules().empty());
}

TEST_F(FileDecorationRulesStoreTest, SaveThenLoadRoundTripsOrderAndEveryField)
{
    FileDecorationRulesStore store(m_rulesFilePath);

    Result<FileDecorationRule> firstRule =
        FileDecorationRule::create("*.exe,*.dll", std::string("#FF0000"), std::nullopt, std::nullopt, true, false, false, false);
    Result<FileDecorationRule> secondRule =
        FileDecorationRule::create("#.git", std::nullopt, std::string("Consolas"), 16, false, true, true, true);
    ASSERT_TRUE(firstRule);
    ASSERT_TRUE(secondRule);

    FileDecorationRules rules;
    rules.setRules({ std::move(firstRule).value(), std::move(secondRule).value() });

    ASSERT_TRUE(store.save(rules));

    const FileDecorationRules loaded = store.load();
    ASSERT_EQ(loaded.rules().size(), 2u);

    const FileDecorationRule& loadedFirst = loaded.rules()[0];
    EXPECT_EQ(loadedFirst.patternsRaw(), "*.exe,*.dll");
    EXPECT_EQ(loadedFirst.hexColor(), "#FF0000");
    EXPECT_EQ(loadedFirst.fontFamily(), std::nullopt);
    EXPECT_TRUE(loadedFirst.bold());
    EXPECT_FALSE(loadedFirst.italic());

    const FileDecorationRule& loadedSecond = loaded.rules()[1];
    EXPECT_EQ(loadedSecond.patternsRaw(), "#.git");
    EXPECT_EQ(loadedSecond.hexColor(), std::nullopt);
    EXPECT_EQ(loadedSecond.fontFamily(), "Consolas");
    EXPECT_TRUE(loadedSecond.italic());
    EXPECT_TRUE(loadedSecond.underline());
    EXPECT_TRUE(loadedSecond.strikeout());
}

TEST_F(FileDecorationRulesStoreTest, LoadOnMalformedJsonReturnsEmptyRuleList)
{
    {
        std::ofstream file(m_rulesFilePath);
        file << "{ not valid json";
    }

    FileDecorationRulesStore store(m_rulesFilePath);
    const FileDecorationRules rules = store.load();

    EXPECT_TRUE(rules.rules().empty());
}
