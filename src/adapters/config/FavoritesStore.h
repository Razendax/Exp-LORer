#pragma once

#include <filesystem>

#include "Favorites.h"

// Loads/saves Favorites as JSON via Qt's QJsonDocument (Architecture.md §14.35) -- same tolerant-
// load/atomic-save shape as FileDecorationRulesStore, in its own favorites.json (a persistent user
// preference, edited from the Favorites menu / Manage Favorites dialog and saved immediately).
class FavoritesStore
{
public:
    explicit FavoritesStore(std::filesystem::path favoritesFilePath);

    Favorites load() const;
    bool save(const Favorites& favorites) const;

private:
    std::filesystem::path m_favoritesFilePath;
};
