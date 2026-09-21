#include <gtest/gtest.h>

#include "FileDecorationRules.h"

namespace
{
    FileDecorationRule makeRule(std::string patternsRaw, std::optional<std::string> hexColor = std::nullopt)
    {
        Result<FileDecorationRule> result =
            FileDecorationRule::create(std::move(patternsRaw), std::move(hexColor), std::nullopt, std::nullopt, false, false, false, false);
        EXPECT_TRUE(result);
        return std::move(result).value();
    }
}

TEST(FileDecorationRules, ResolveReturnsNullptrWhenNoRuleMatches)
{
    FileDecorationRules rules;
    rules.setRules({ makeRule("*.exe") });

    EXPECT_EQ(rules.resolve("readme.txt", false), nullptr);
}

TEST(FileDecorationRules, ResolveReturnsTheFirstMatchingRuleInListOrder)
{
    FileDecorationRules rules;
    rules.setRules({
        makeRule("*.txt", std::string("#111111")),
        makeRule("*.txt", std::string("#222222")),
    });

    const FileDecorationRule* resolved = rules.resolve("notes.txt", false);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->hexColor(), "#111111");
}

TEST(FileDecorationRules, ResolveSkipsNonMatchingRulesToFindALaterMatch)
{
    FileDecorationRules rules;
    rules.setRules({
        makeRule("*.exe", std::string("#111111")),
        makeRule("*.txt", std::string("#222222")),
    });

    const FileDecorationRule* resolved = rules.resolve("notes.txt", false);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->hexColor(), "#222222");
}

TEST(FileDecorationRules, ResolveOnEmptyRuleListReturnsNullptr)
{
    FileDecorationRules rules;

    EXPECT_EQ(rules.resolve("anything", false), nullptr);
}

TEST(FileDecorationRules, DefaultConstructedExposesNoOverrideHiddenRules)
{
    FileDecorationRules rules;

    EXPECT_EQ(rules.hiddenFilesRule().hexColor(), std::nullopt);
    EXPECT_EQ(rules.hiddenFoldersRule().hexColor(), std::nullopt);
    EXPECT_EQ(rules.hiddenStyle(false).hexColor(), std::nullopt);
    EXPECT_EQ(rules.hiddenStyle(true).hexColor(), std::nullopt);
}

TEST(FileDecorationRules, SetHiddenFilesAndFoldersRulesRoundTrip)
{
    FileDecorationRules rules;
    rules.setHiddenFilesRule(makeRule("", std::string("#111111")));
    rules.setHiddenFoldersRule(makeRule("", std::string("#222222")));

    EXPECT_EQ(rules.hiddenFilesRule().hexColor(), "#111111");
    EXPECT_EQ(rules.hiddenFoldersRule().hexColor(), "#222222");
    EXPECT_EQ(rules.hiddenStyle(false).hexColor(), "#111111");
    EXPECT_EQ(rules.hiddenStyle(true).hexColor(), "#222222");
}

TEST(FileDecorationRules, HiddenRulesAreExcludedFromRulesAndResolve)
{
    FileDecorationRules rules;
    rules.setRules({ makeRule("*.txt", std::string("#333333")) });
    rules.setHiddenFilesRule(makeRule("*", std::string("#111111")));
    rules.setHiddenFoldersRule(makeRule("*", std::string("#222222")));

    EXPECT_EQ(rules.rules().size(), 1u);
    // Even though the hidden rule's pattern list would match "notes.txt", resolve() must never
    // return it -- it's structurally excluded from m_rules, not just conventionally.
    const FileDecorationRule* resolved = rules.resolve("notes.txt", false);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->hexColor(), "#333333");
}
