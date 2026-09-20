#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "HighlightTheme.h"
#include "HighlightThemeStore.h"

namespace
{
    namespace fs = std::filesystem;
}

TEST(HighlightTheme, ColorForFallsBackToBuiltInDefaultWhenNoOverrideExists)
{
    HighlightTheme theme;

    EXPECT_EQ(theme.colorFor(Language::Cpp, "keyword"), "#0000FF");
    EXPECT_EQ(theme.colorFor(Language::Cpp, "string.escape"), "#008000"); // "string.escape" -> "string" category
}

TEST(HighlightTheme, ColorForReturnsNulloptForTokensWithNoBuiltInDefault)
{
    HighlightTheme theme;

    // "variable"/"operator" (and anything else outside the six-category base palette) are left at
    // the preview text view's own default foreground color by design (Architecture.md §14.26).
    EXPECT_EQ(theme.colorFor(Language::Cpp, "variable"), std::nullopt);
    EXPECT_EQ(theme.colorFor(Language::Cpp, "operator"), std::nullopt);
}

TEST(HighlightTheme, SetTokenColorOverridesTheBuiltInDefault)
{
    HighlightTheme theme;

    EXPECT_EQ(theme.overrideFor(Language::Python, "keyword"), std::nullopt);

    theme.setTokenColor(Language::Python, "keyword", "#123456");

    EXPECT_EQ(theme.overrideFor(Language::Python, "keyword"), "#123456");
    EXPECT_EQ(theme.colorFor(Language::Python, "keyword"), "#123456");
}

TEST(HighlightTheme, OverridesAreIndependentPerLanguage)
{
    HighlightTheme theme;

    theme.setTokenColor(Language::Cpp, "keyword", "#111111");

    EXPECT_EQ(theme.overrideFor(Language::Cpp, "keyword"), "#111111");
    EXPECT_EQ(theme.overrideFor(Language::Python, "keyword"), std::nullopt);
}

namespace
{
    class HighlightThemeStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_highlight_theme_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_themeFilePath = m_tempDir / "highlight_theme.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_themeFilePath;
    };
}

TEST_F(HighlightThemeStoreTest, LoadOnMissingFileReturnsThemeWithNoOverrides)
{
    HighlightThemeStore store(m_themeFilePath);

    const HighlightTheme theme = store.load();

    EXPECT_TRUE(theme.overrides().empty());
}

TEST_F(HighlightThemeStoreTest, SaveThenLoadRoundTripsOverrides)
{
    HighlightThemeStore store(m_themeFilePath);

    HighlightTheme theme;
    theme.setTokenColor(Language::Cpp, "keyword", "#ABCDEF");
    theme.setTokenColor(Language::Json, "number", "#112233");

    ASSERT_TRUE(store.save(theme));

    const HighlightTheme loaded = store.load();

    EXPECT_EQ(loaded.overrideFor(Language::Cpp, "keyword"), "#ABCDEF");
    EXPECT_EQ(loaded.overrideFor(Language::Json, "number"), "#112233");
    EXPECT_EQ(loaded.overrideFor(Language::Python, "keyword"), std::nullopt);
}

TEST_F(HighlightThemeStoreTest, LoadOnMalformedJsonReturnsThemeWithNoOverrides)
{
    {
        std::ofstream file(m_themeFilePath);
        file << "{ not valid json";
    }

    HighlightThemeStore store(m_themeFilePath);
    const HighlightTheme theme = store.load();

    EXPECT_TRUE(theme.overrides().empty());
}
