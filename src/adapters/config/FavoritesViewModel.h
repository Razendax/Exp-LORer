#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include "AsyncSaveDispatcher.h"
#include "Favorites.h"
#include "FavoritesStore.h"

// Thin Qt-facing wrapper around Favorites + FavoritesStore (Architecture.md §14.35), owned once by
// CompositionRoot so the Favorites menu (MainWindow) and the Manage Favorites dialog share one
// live instance. Every mutation persists immediately and broadcasts favoritesChanged() so both
// stay current -- the Favorites menu rebuilds lazily on aboutToShow, ManageFavoritesDialog rebuilds
// its tree on this signal. The actual FavoritesStore::save() call runs on a dedicated background
// thread (via m_saveDispatcher), so no mutation ever blocks the Qt UI thread on disk I/O.
class FavoritesViewModel : public QObject
{
    Q_OBJECT

public:
    FavoritesViewModel(Favorites& favorites, FavoritesStore& store, QObject* parent = nullptr);

    const std::vector<Favorites::Entry>& entries() const noexcept { return m_favorites.entries(); }

    Favorites::Id addItem(const std::filesystem::path& path);
    Favorites::Id addCategory(const QString& name);
    void renameCategory(Favorites::Id categoryId, const QString& name);

    // Removes a top-level item or a category (flattening its items back to top-level).
    void removeEntry(Favorites::Id id);
    void removeItemFromCategory(Favorites::Id categoryId, Favorites::Id itemId);

    // nullopt moves the item to top-level; otherwise moves it into the given category.
    void moveItemToCategory(Favorites::Id itemId, std::optional<Favorites::Id> categoryId);

    void moveEntryUp(Favorites::Id id);
    void moveEntryDown(Favorites::Id id);
    void moveItemUpWithinCategory(Favorites::Id categoryId, Favorites::Id itemId);
    void moveItemDownWithinCategory(Favorites::Id categoryId, Favorites::Id itemId);

signals:
    void favoritesChanged();

private:
    // Common "emit favoritesChanged() -> queue a background save" tail shared by every mutator
    // above, called after the mutation has already been applied to m_favorites.
    void afterMutation();

    Favorites& m_favorites;
    FavoritesStore& m_store;

    AsyncSaveDispatcher<Favorites> m_saveDispatcher;
};
