#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "FavoritesStore.h"

namespace
{
    namespace fs = std::filesystem;

    class FavoritesStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_favorites_store_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
            m_favoritesFilePath = m_tempDir / "favorites.json";
        }

        void TearDown() override { fs::remove_all(m_tempDir); }

        fs::path m_tempDir;
        fs::path m_favoritesFilePath;
    };

    const FavoriteItem* asItem(const Favorites::Entry& entry)
    {
        return std::get_if<FavoriteItem>(&entry);
    }

    const FavoriteCategory* asCategory(const Favorites::Entry& entry)
    {
        return std::get_if<FavoriteCategory>(&entry);
    }
}

TEST_F(FavoritesStoreTest, LoadOnMissingFileReturnsEmptyFavorites)
{
    FavoritesStore store(m_favoritesFilePath);

    const Favorites favorites = store.load();

    EXPECT_TRUE(favorites.entries().empty());
}

TEST_F(FavoritesStoreTest, SaveThenLoadRoundTripsATopLevelItemAndACategoryWithItsItemsInOrder)
{
    FavoritesStore store(m_favoritesFilePath);

    Favorites favorites;
    const Favorites::Id topLevelId = favorites.addItem("C:/Users/alice/Documents");
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id firstItemId = favorites.addItem("C:/Git/Exp-LORer");
    const Favorites::Id secondItemId = favorites.addItem("D:/scratch");
    favorites.moveItemToCategory(firstItemId, categoryId);
    favorites.moveItemToCategory(secondItemId, categoryId);

    ASSERT_TRUE(store.save(favorites));

    const Favorites loaded = store.load();
    ASSERT_EQ(loaded.entries().size(), 2u);

    const FavoriteItem* loadedTopLevelItem = asItem(loaded.entries()[0]);
    ASSERT_NE(loadedTopLevelItem, nullptr);
    EXPECT_EQ(loadedTopLevelItem->id(), topLevelId);
    EXPECT_EQ(loadedTopLevelItem->path(), "C:/Users/alice/Documents");

    const FavoriteCategory* loadedCategory = asCategory(loaded.entries()[1]);
    ASSERT_NE(loadedCategory, nullptr);
    EXPECT_EQ(loadedCategory->id(), categoryId);
    EXPECT_EQ(loadedCategory->name(), "Projects");
    ASSERT_EQ(loadedCategory->items().size(), 2u);
    EXPECT_EQ(loadedCategory->items()[0].id(), firstItemId);
    EXPECT_EQ(loadedCategory->items()[0].path(), "C:/Git/Exp-LORer");
    EXPECT_EQ(loadedCategory->items()[1].id(), secondItemId);
    EXPECT_EQ(loadedCategory->items()[1].path(), "D:/scratch");
}

TEST_F(FavoritesStoreTest, SaveThenLoadRoundTripsNextIdSoFutureIdsStayMonotonic)
{
    FavoritesStore store(m_favoritesFilePath);

    Favorites favorites;
    favorites.addItem("C:/a");
    favorites.addItem("C:/b");

    ASSERT_TRUE(store.save(favorites));

    const Favorites loaded = store.load();
    EXPECT_EQ(loaded.nextId(), favorites.nextId());
}

TEST_F(FavoritesStoreTest, LoadSkipsAnEntryWithAnUnrecognizedKind)
{
    {
        std::ofstream file(m_favoritesFilePath);
        file << R"({ "version": 1, "nextId": 2, "entries": [ { "kind": "bogus", "id": 1 } ] })";
    }

    FavoritesStore store(m_favoritesFilePath);
    const Favorites favorites = store.load();

    EXPECT_TRUE(favorites.entries().empty());
}

TEST_F(FavoritesStoreTest, LoadSkipsAnItemMissingItsPath)
{
    {
        std::ofstream file(m_favoritesFilePath);
        file << R"({ "version": 1, "nextId": 2, "entries": [ { "kind": "item", "id": 1 } ] })";
    }

    FavoritesStore store(m_favoritesFilePath);
    const Favorites favorites = store.load();

    EXPECT_TRUE(favorites.entries().empty());
}

TEST_F(FavoritesStoreTest, LoadSkipsAMalformedItemInsideAWellFormedCategory)
{
    {
        std::ofstream file(m_favoritesFilePath);
        file << R"({ "version": 1, "nextId": 3, "entries": [
                 { "kind": "category", "id": 1, "name": "Projects", "items": [
                     { "id": 2 },
                     { "id": 3, "path": "C:/kept" }
                 ] }
             ] })";
    }

    FavoritesStore store(m_favoritesFilePath);
    const Favorites favorites = store.load();

    ASSERT_EQ(favorites.entries().size(), 1u);
    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    ASSERT_NE(category, nullptr);
    ASSERT_EQ(category->items().size(), 1u);
    EXPECT_EQ(category->items()[0].path(), "C:/kept");
}

TEST_F(FavoritesStoreTest, LoadOnMalformedJsonReturnsEmptyFavorites)
{
    {
        std::ofstream file(m_favoritesFilePath);
        file << "{ not valid json";
    }

    FavoritesStore store(m_favoritesFilePath);
    const Favorites favorites = store.load();

    EXPECT_TRUE(favorites.entries().empty());
}
