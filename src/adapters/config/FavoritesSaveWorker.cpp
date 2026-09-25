#include "FavoritesSaveWorker.h"

#include "Favorites.h"
#include "FavoritesStore.h"

FavoritesSaveWorker::FavoritesSaveWorker(FavoritesStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
}

void FavoritesSaveWorker::save(const Favorites& favorites) const
{
    m_store.save(favorites);
}
