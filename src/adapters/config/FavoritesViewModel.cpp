#include "FavoritesViewModel.h"

#include <QMetaObject>

#include "FavoritesSaveWorker.h"

FavoritesViewModel::FavoritesViewModel(Favorites& favorites, FavoritesStore& store, QObject* parent)
    : QObject(parent)
    , m_favorites(favorites)
    , m_store(store)
{
    m_saveWorker = new FavoritesSaveWorker(m_store);
    m_saveWorker->moveToThread(&m_saveThread);
    m_saveThread.start();
}

FavoritesViewModel::~FavoritesViewModel()
{
    m_saveThread.quit();
    m_saveThread.wait();
    delete m_saveWorker;
}

Favorites::Id FavoritesViewModel::addItem(const std::filesystem::path& path)
{
    const Favorites::Id id = m_favorites.addItem(path);
    afterMutation();
    return id;
}

Favorites::Id FavoritesViewModel::addCategory(const QString& name)
{
    const Favorites::Id id = m_favorites.addCategory(name.toStdString());
    afterMutation();
    return id;
}

void FavoritesViewModel::renameCategory(Favorites::Id categoryId, const QString& name)
{
    m_favorites.renameCategory(categoryId, name.toStdString());
    afterMutation();
}

void FavoritesViewModel::removeEntry(Favorites::Id id)
{
    m_favorites.removeEntry(id);
    afterMutation();
}

void FavoritesViewModel::removeItemFromCategory(Favorites::Id categoryId, Favorites::Id itemId)
{
    m_favorites.removeItemFromCategory(categoryId, itemId);
    afterMutation();
}

void FavoritesViewModel::moveItemToCategory(Favorites::Id itemId, std::optional<Favorites::Id> categoryId)
{
    m_favorites.moveItemToCategory(itemId, categoryId);
    afterMutation();
}

void FavoritesViewModel::moveEntryUp(Favorites::Id id)
{
    m_favorites.moveEntryUp(id);
    afterMutation();
}

void FavoritesViewModel::moveEntryDown(Favorites::Id id)
{
    m_favorites.moveEntryDown(id);
    afterMutation();
}

void FavoritesViewModel::moveItemUpWithinCategory(Favorites::Id categoryId, Favorites::Id itemId)
{
    m_favorites.moveItemUpWithinCategory(categoryId, itemId);
    afterMutation();
}

void FavoritesViewModel::moveItemDownWithinCategory(Favorites::Id categoryId, Favorites::Id itemId)
{
    m_favorites.moveItemDownWithinCategory(categoryId, itemId);
    afterMutation();
}

void FavoritesViewModel::afterMutation()
{
    emit favoritesChanged();

    // Save runs on m_saveThread -- Favorites is a plain copyable value type, so the worker gets
    // its own independent snapshot rather than touching m_favorites from another thread.
    QMetaObject::invokeMethod(
        m_saveWorker, [worker = m_saveWorker, favorites = m_favorites]() { worker->save(favorites); }, Qt::QueuedConnection);
}
