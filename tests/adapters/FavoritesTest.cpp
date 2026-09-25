#include <gtest/gtest.h>

#include "Favorites.h"

namespace
{
    const FavoriteItem* asItem(const Favorites::Entry& entry)
    {
        return std::get_if<FavoriteItem>(&entry);
    }

    const FavoriteCategory* asCategory(const Favorites::Entry& entry)
    {
        return std::get_if<FavoriteCategory>(&entry);
    }
}

TEST(Favorites, AddItemAppendsATopLevelUncategorizedEntry)
{
    Favorites favorites;

    const Favorites::Id id = favorites.addItem("C:/Users/alice/Documents");

    ASSERT_EQ(favorites.entries().size(), 1u);
    const FavoriteItem* item = asItem(favorites.entries()[0]);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->id(), id);
    EXPECT_EQ(item->path(), "C:/Users/alice/Documents");
}

TEST(Favorites, AddItemDedupsAnAlreadyFavoritedTopLevelPath)
{
    Favorites favorites;

    const Favorites::Id first = favorites.addItem("C:/Projects");
    const Favorites::Id second = favorites.addItem("C:/Projects");

    EXPECT_EQ(first, second);
    EXPECT_EQ(favorites.entries().size(), 1u);
}

TEST(Favorites, AddItemDedupsAPathAlreadyFavoritedInsideACategory)
{
    Favorites favorites;

    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id itemId = favorites.addItem("C:/Projects/Exp-LORer");
    favorites.moveItemToCategory(itemId, categoryId);

    const Favorites::Id dedupedId = favorites.addItem("C:/Projects/Exp-LORer");

    EXPECT_EQ(dedupedId, itemId);
    // No new top-level entry was created for the dedup -- still just the category.
    EXPECT_EQ(favorites.entries().size(), 1u);
}

TEST(Favorites, AddCategoryAppendsATopLevelEntry)
{
    Favorites favorites;

    const Favorites::Id id = favorites.addCategory("Projects");

    ASSERT_EQ(favorites.entries().size(), 1u);
    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->id(), id);
    EXPECT_EQ(category->name(), "Projects");
    EXPECT_TRUE(category->items().empty());
}

TEST(Favorites, RenameCategoryUpdatesItsName)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Old Name");

    favorites.renameCategory(categoryId, "New Name");

    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->name(), "New Name");
}

TEST(Favorites, RemoveEntryRemovesATopLevelItem)
{
    Favorites favorites;
    const Favorites::Id id = favorites.addItem("C:/a");
    favorites.addItem("C:/b");

    favorites.removeEntry(id);

    ASSERT_EQ(favorites.entries().size(), 1u);
    EXPECT_EQ(asItem(favorites.entries()[0])->path(), "C:/b");
}

TEST(Favorites, RemoveEntryOnACategoryFlattensItsItemsBackToTopLevelInPriorOrder)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id firstItemId = favorites.addItem("C:/first");
    const Favorites::Id secondItemId = favorites.addItem("C:/second");
    favorites.moveItemToCategory(firstItemId, categoryId);
    favorites.moveItemToCategory(secondItemId, categoryId);

    favorites.removeEntry(categoryId);

    ASSERT_EQ(favorites.entries().size(), 2u);
    const FavoriteItem* first = asItem(favorites.entries()[0]);
    const FavoriteItem* second = asItem(favorites.entries()[1]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->id(), firstItemId);
    EXPECT_EQ(second->id(), secondItemId);
}

TEST(Favorites, RemoveItemFromCategoryRemovesOnlyThatItem)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id keepId = favorites.addItem("C:/keep");
    const Favorites::Id removeId = favorites.addItem("C:/remove");
    favorites.moveItemToCategory(keepId, categoryId);
    favorites.moveItemToCategory(removeId, categoryId);

    favorites.removeItemFromCategory(categoryId, removeId);

    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    ASSERT_EQ(category->items().size(), 1u);
    EXPECT_EQ(category->items()[0].id(), keepId);
}

TEST(Favorites, MoveItemToCategoryMovesATopLevelItemIntoTheCategory)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id itemId = favorites.addItem("C:/a");

    favorites.moveItemToCategory(itemId, categoryId);

    ASSERT_EQ(favorites.entries().size(), 1u);
    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    ASSERT_EQ(category->items().size(), 1u);
    EXPECT_EQ(category->items()[0].id(), itemId);
}

TEST(Favorites, MoveItemToCategoryWithNulloptMovesItBackToTopLevel)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id itemId = favorites.addItem("C:/a");
    favorites.moveItemToCategory(itemId, categoryId);

    favorites.moveItemToCategory(itemId, std::nullopt);

    ASSERT_EQ(favorites.entries().size(), 2u);
    const FavoriteItem* topLevelItem = asItem(favorites.entries()[1]);
    ASSERT_NE(topLevelItem, nullptr);
    EXPECT_EQ(topLevelItem->id(), itemId);
    EXPECT_TRUE(asCategory(favorites.entries()[0])->items().empty());
}

TEST(Favorites, MoveItemToCategoryIsNoopWhenTargetCategoryDoesNotExist)
{
    Favorites favorites;
    const Favorites::Id itemId = favorites.addItem("C:/a");

    favorites.moveItemToCategory(itemId, 12345);

    ASSERT_EQ(favorites.entries().size(), 1u);
    EXPECT_EQ(asItem(favorites.entries()[0])->id(), itemId);
}

TEST(Favorites, MoveEntryUpSwapsWithThePreviousTopLevelSibling)
{
    Favorites favorites;
    favorites.addItem("C:/a");
    const Favorites::Id secondId = favorites.addItem("C:/b");

    favorites.moveEntryUp(secondId);

    EXPECT_EQ(asItem(favorites.entries()[0])->id(), secondId);
    EXPECT_EQ(asItem(favorites.entries()[1])->path(), "C:/a");
}

TEST(Favorites, MoveEntryUpOnTheFirstEntryIsNoop)
{
    Favorites favorites;
    const Favorites::Id firstId = favorites.addItem("C:/a");
    favorites.addItem("C:/b");

    favorites.moveEntryUp(firstId);

    EXPECT_EQ(asItem(favorites.entries()[0])->id(), firstId);
}

TEST(Favorites, MoveEntryDownSwapsWithTheNextTopLevelSibling)
{
    Favorites favorites;
    const Favorites::Id firstId = favorites.addItem("C:/a");
    favorites.addItem("C:/b");

    favorites.moveEntryDown(firstId);

    EXPECT_EQ(asItem(favorites.entries()[1])->id(), firstId);
    EXPECT_EQ(asItem(favorites.entries()[0])->path(), "C:/b");
}

TEST(Favorites, MoveItemUpWithinCategorySwapsWithThePreviousItemInThatCategory)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id firstId = favorites.addItem("C:/a");
    const Favorites::Id secondId = favorites.addItem("C:/b");
    favorites.moveItemToCategory(firstId, categoryId);
    favorites.moveItemToCategory(secondId, categoryId);

    favorites.moveItemUpWithinCategory(categoryId, secondId);

    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    EXPECT_EQ(category->items()[0].id(), secondId);
    EXPECT_EQ(category->items()[1].id(), firstId);
}

TEST(Favorites, MoveItemDownWithinCategorySwapsWithTheNextItemInThatCategory)
{
    Favorites favorites;
    const Favorites::Id categoryId = favorites.addCategory("Projects");
    const Favorites::Id firstId = favorites.addItem("C:/a");
    const Favorites::Id secondId = favorites.addItem("C:/b");
    favorites.moveItemToCategory(firstId, categoryId);
    favorites.moveItemToCategory(secondId, categoryId);

    favorites.moveItemDownWithinCategory(categoryId, firstId);

    const FavoriteCategory* category = asCategory(favorites.entries()[0]);
    EXPECT_EQ(category->items()[0].id(), secondId);
    EXPECT_EQ(category->items()[1].id(), firstId);
}

TEST(Favorites, IdsAreMonotonicAndNeverCollide)
{
    Favorites favorites;

    const Favorites::Id first = favorites.addItem("C:/a");
    const Favorites::Id second = favorites.addCategory("Projects");
    const Favorites::Id third = favorites.addItem("C:/b");

    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_NE(first, third);
}

TEST(Favorites, SetNextIdIsHonoredByFollowingAllocations)
{
    Favorites favorites;
    favorites.setNextId(100);

    const Favorites::Id id = favorites.addItem("C:/a");

    EXPECT_EQ(id, 100);
    EXPECT_EQ(favorites.nextId(), 101);
}
