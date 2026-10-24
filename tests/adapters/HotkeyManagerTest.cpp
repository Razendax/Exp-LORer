#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "HotkeyManager.h"
#include "HotkeyStore.h"

namespace
{
    namespace fs = std::filesystem;

    QKeySequence chord(const QString& text)
    {
        return QKeySequence(text, QKeySequence::PortableText);
    }

    // A small, self-contained catalog (NOT HotkeyCatalog::entries()) so this test fully controls
    // scope/allowsBareRebind/default-chord collisions rather than depending on the real catalog
    // staying collision-free. Reuses real HotkeyAction enumerators as convenient stand-ins -- their
    // real-world meaning is irrelevant here.
    std::vector<HotkeyCatalogEntry> testCatalog()
    {
        using Category = HotkeyCatalogEntry::Category;
        using Scope = HotkeyCatalogEntry::Scope;

        std::vector<HotkeyCatalogEntry> entries;
        entries.push_back({ HotkeyAction::Copy, "test.copy", Category::FileOperations, QStringLiteral("Copy"),
                             HotkeyChord(chord(QStringLiteral("Ctrl+C"))), Scope::View, false });
        entries.push_back({ HotkeyAction::Cut, "test.cut", Category::FileOperations, QStringLiteral("Cut"),
                             HotkeyChord(chord(QStringLiteral("Ctrl+X"))), Scope::View, false });
        entries.push_back({ HotkeyAction::Rename, "test.rename", Category::FileOperations, QStringLiteral("Rename"),
                             HotkeyChord(chord(QStringLiteral("F2"))), Scope::View, true });
        // Window-scoped stand-in sharing Copy's own default chord -- same chord, different scope,
        // must never be reported as a conflict (spec §5: cross-scope duplicates never contend).
        entries.push_back({ HotkeyAction::Paste, "test.windowPaste", Category::Tabs, QStringLiteral("Window Paste"),
                             HotkeyChord(chord(QStringLiteral("Ctrl+C"))), Scope::Window, false });
        return entries;
    }

    class HotkeyManagerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_hotkey_manager_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_store = std::make_unique<HotkeyStore>(m_tempDir / "hotkeys.json");
            m_catalog = testCatalog();
            m_manager = std::make_unique<HotkeyManager>(*m_store, m_catalog);
        }

        void TearDown() override
        {
            m_manager.reset();
            m_store.reset();
            fs::remove_all(m_tempDir);
        }

        fs::path m_tempDir;
        std::vector<HotkeyCatalogEntry> m_catalog;
        std::unique_ptr<HotkeyStore> m_store;
        std::unique_ptr<HotkeyManager> m_manager;
    };
}

TEST_F(HotkeyManagerTest, ChordForReturnsTheCatalogDefaultWithNoOverride)
{
    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Copy), chord(QStringLiteral("Ctrl+C")));
}

TEST_F(HotkeyManagerTest, SetBindingRejectsABareKeyWhenTheActionDoesNotAllowIt)
{
    const bool accepted = m_manager->setBinding(HotkeyAction::Copy, chord(QStringLiteral("A")));

    EXPECT_FALSE(accepted);
    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Copy), chord(QStringLiteral("Ctrl+C"))); // unchanged
}

TEST_F(HotkeyManagerTest, SetBindingAcceptsABareKeyWhenTheActionAllowsBareRebind)
{
    const bool accepted = m_manager->setBinding(HotkeyAction::Rename, chord(QStringLiteral("F3")));

    EXPECT_TRUE(accepted);
    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Rename), chord(QStringLiteral("F3")));
}

TEST_F(HotkeyManagerTest, ConflictingActionFindsAnotherSameScopeActionBoundToTheSameChord)
{
    ASSERT_TRUE(m_manager->setBinding(HotkeyAction::Cut, chord(QStringLiteral("Ctrl+C"))));

    const auto conflict = m_manager->conflictingAction(HotkeyAction::Copy, chord(QStringLiteral("Ctrl+C")));

    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(*conflict, HotkeyAction::Cut);
}

TEST_F(HotkeyManagerTest, ConflictingActionIgnoresADifferentScopeSharingTheSameChord)
{
    // test.windowPaste's own default is Ctrl+C too, but it's Window-scoped -- never a conflict
    // against Copy's own View-scoped Ctrl+C.
    const auto conflict = m_manager->conflictingAction(HotkeyAction::Copy, chord(QStringLiteral("Ctrl+C")));

    EXPECT_FALSE(conflict.has_value());
}

TEST_F(HotkeyManagerTest, ResolveReturnsTheMostRecentlyBoundActionAmongSameScopeDuplicates)
{
    // Cut is rebound onto Ctrl+C, same chord Copy already sits at by (unbound) default -- the
    // explicitly-bound action must win over one still at its default (spec §5).
    ASSERT_TRUE(m_manager->setBinding(HotkeyAction::Cut, chord(QStringLiteral("Ctrl+C"))));

    const auto resolved = m_manager->resolve(HotkeyCatalogEntry::Scope::View, chord(QStringLiteral("Ctrl+C")));

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, HotkeyAction::Cut);
}

TEST_F(HotkeyManagerTest, ResolveNeverCrossesScopeBoundaries)
{
    // Copy (View) and test.windowPaste (Window) both default to Ctrl+C -- with every
    // scope/category now coexisting for real (M7 spot-check), each scope's own resolve() call must
    // independently return its own action, unaffected by the other scope sharing the exact chord.
    const auto windowResolved = m_manager->resolve(HotkeyCatalogEntry::Scope::Window, chord(QStringLiteral("Ctrl+C")));
    ASSERT_TRUE(windowResolved.has_value());
    EXPECT_EQ(*windowResolved, HotkeyAction::Paste); // the Window-scoped stand-in, not View-scoped Copy

    const auto viewResolved = m_manager->resolve(HotkeyCatalogEntry::Scope::View, chord(QStringLiteral("Ctrl+C")));
    ASSERT_TRUE(viewResolved.has_value());
    EXPECT_EQ(*viewResolved, HotkeyAction::Copy); // the View-scoped action, unaffected by Window's Paste
}

TEST_F(HotkeyManagerTest, ClearBindingMakesResolveReturnNulloptForThatChordWhenNothingElseIsBoundToIt)
{
    m_manager->clearBinding(HotkeyAction::Copy);

    const auto resolved = m_manager->resolve(HotkeyCatalogEntry::Scope::View, chord(QStringLiteral("Ctrl+C")));

    EXPECT_FALSE(resolved.has_value());
}

TEST_F(HotkeyManagerTest, ResetToDefaultRestoresTheCatalogDefaultChord)
{
    m_manager->setBinding(HotkeyAction::Rename, chord(QStringLiteral("F3")));

    m_manager->resetToDefault(HotkeyAction::Rename);

    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Rename), chord(QStringLiteral("F2")));
}

TEST_F(HotkeyManagerTest, ResetAllRestoresEveryActionToItsCatalogDefault)
{
    m_manager->setBinding(HotkeyAction::Rename, chord(QStringLiteral("F3")));
    m_manager->clearBinding(HotkeyAction::Cut);

    m_manager->resetAll();

    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Rename), chord(QStringLiteral("F2")));
    EXPECT_EQ(m_manager->chordFor(HotkeyAction::Cut), chord(QStringLiteral("Ctrl+X")));
}

TEST_F(HotkeyManagerTest, BindingsChangedFiresOnAnAcceptedMutationButNotOnARejectedOne)
{
    int signalCount = 0;
    QObject::connect(m_manager.get(), &HotkeyManager::bindingsChanged, [&signalCount]() { ++signalCount; });

    EXPECT_FALSE(m_manager->setBinding(HotkeyAction::Copy, chord(QStringLiteral("A")))); // rejected: bare key
    EXPECT_EQ(signalCount, 0);

    EXPECT_TRUE(m_manager->setBinding(HotkeyAction::Rename, chord(QStringLiteral("F3")))); // accepted: allowsBareRebind
    EXPECT_EQ(signalCount, 1);
}
