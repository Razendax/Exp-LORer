#pragma once

#include <QObject>

class Favorites;
class FavoritesStore;

// Runs FavoritesStore::save on whatever thread it has been moved to (Architecture.md §14.35) --
// same "worker QObject moved to a dedicated QThread, dispatched via a queued functor" shape
// FileDecorationRulesSaveWorker already establishes -- disk I/O must never run on the Qt UI thread.
class FavoritesSaveWorker : public QObject
{
    Q_OBJECT

public:
    explicit FavoritesSaveWorker(FavoritesStore& store, QObject* parent = nullptr);

    void save(const Favorites& favorites) const;

private:
    FavoritesStore& m_store;
};
