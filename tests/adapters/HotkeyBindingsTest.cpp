#include <gtest/gtest.h>

#include "HotkeyBindings.h"

namespace
{
    HotkeyChord chordFromText(const QString& text)
    {
        return HotkeyChord(QKeySequence(text, QKeySequence::PortableText));
    }
}

TEST(HotkeyBindings, EffectiveChordFallsBackToDefaultWhenNoOverrideExists)
{
    HotkeyBindings bindings;
    const HotkeyChord defaultChord = chordFromText("Ctrl+C");

    EXPECT_EQ(bindings.effectiveChord("fileOps.copy", defaultChord), defaultChord);
}

TEST(HotkeyBindings, SetBindingOverridesTheEffectiveChord)
{
    HotkeyBindings bindings;
    const HotkeyChord defaultChord = chordFromText("Ctrl+C");
    const HotkeyChord rebound = chordFromText("Ctrl+Shift+C");

    bindings.setBinding("fileOps.copy", rebound);

    EXPECT_EQ(bindings.effectiveChord("fileOps.copy", defaultChord), rebound);
}

TEST(HotkeyBindings, ClearBindingStoresAnExplicitEmptyOverrideDistinctFromNoOverride)
{
    HotkeyBindings bindings;
    const HotkeyChord defaultChord = chordFromText("Ctrl+C");

    bindings.clearBinding("fileOps.copy");

    EXPECT_TRUE(bindings.effectiveChord("fileOps.copy", defaultChord).isEmpty());
    ASSERT_EQ(bindings.overrides().count("fileOps.copy"), 1u);
    EXPECT_TRUE(bindings.overrides().at("fileOps.copy").chord.isEmpty());
}

TEST(HotkeyBindings, ResetToDefaultErasesTheOverrideEntirely)
{
    HotkeyBindings bindings;
    const HotkeyChord defaultChord = chordFromText("Ctrl+C");
    bindings.setBinding("fileOps.copy", chordFromText("Ctrl+Shift+C"));

    bindings.resetToDefault("fileOps.copy");

    EXPECT_EQ(bindings.overrides().count("fileOps.copy"), 0u);
    EXPECT_EQ(bindings.effectiveChord("fileOps.copy", defaultChord), defaultChord);
}

TEST(HotkeyBindings, ResetAllClearsEveryOverride)
{
    HotkeyBindings bindings;
    bindings.setBinding("fileOps.copy", chordFromText("Ctrl+Shift+C"));
    bindings.setBinding("fileOps.cut", chordFromText("Ctrl+Shift+X"));

    bindings.resetAll();

    EXPECT_TRUE(bindings.overrides().empty());
}

TEST(HotkeyBindings, SetBindingBumpsTheMonotonicOrderCounterOnEachCall)
{
    HotkeyBindings bindings;

    bindings.setBinding("fileOps.copy", chordFromText("Ctrl+Shift+C"));
    bindings.setBinding("fileOps.cut", chordFromText("Ctrl+Shift+X"));

    EXPECT_LT(bindings.overrides().at("fileOps.copy").order, bindings.overrides().at("fileOps.cut").order);
}

TEST(HotkeyBindings, ClearBindingAlsoBumpsTheOrderCounterForRecencyTieBreaking)
{
    HotkeyBindings bindings;
    bindings.setBinding("fileOps.copy", chordFromText("Ctrl+Shift+C"));
    const std::uint64_t orderAfterSet = bindings.overrides().at("fileOps.copy").order;

    bindings.clearBinding("fileOps.cut");

    EXPECT_GT(bindings.overrides().at("fileOps.cut").order, orderAfterSet);
}

TEST(HotkeyBindings, SetOverridesRestoresOverridesVerbatimWithoutRecomputingNextOrder)
{
    HotkeyBindings bindings;
    std::unordered_map<std::string, HotkeyBindings::Override> restored;
    restored["fileOps.copy"] = HotkeyBindings::Override{ chordFromText("Ctrl+Shift+C"), 7 };

    bindings.setOverrides(restored, 42);

    EXPECT_EQ(bindings.overrides().at("fileOps.copy").order, 7u);
    EXPECT_EQ(bindings.nextOrder(), 42u);

    // The restored nextOrder (not a recomputation from the restored overrides) backs the next
    // setBinding call.
    bindings.setBinding("fileOps.cut", chordFromText("Ctrl+Shift+X"));
    EXPECT_EQ(bindings.overrides().at("fileOps.cut").order, 42u);
}
