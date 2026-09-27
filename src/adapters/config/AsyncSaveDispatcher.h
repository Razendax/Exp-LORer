#pragma once

#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QThread>

#include "AsyncSaveWorker.h"

// Owns the dedicated background QThread + AsyncSaveWorker<T> pair that every *ViewModel in this
// directory (FavoritesViewModel, HighlightThemeViewModel, FileDecorationsViewModel,
// NotificationSettingsViewModel, ColumnVisibilitySettingsViewModel) used to hand-roll identically:
// construct a worker, moveToThread it, start the thread; quit()+wait()+delete it on teardown; queue
// a save via QMetaObject::invokeMethod on every mutation. This collapses all of that into one
// member.
//
// Construct with the store's save call bound to a lambda, e.g.:
//   AsyncSaveDispatcher<Favorites> m_saveDispatcher{ [&store](const Favorites& v) { store.save(v); } };
// then call dispatch(currentValue) after each mutation (from setRules()/afterMutation()/... --
// whatever this ViewModel's own "mutate then persist" tail already looks like).
template <typename T>
class AsyncSaveDispatcher
{
public:
    using SaveFn = typename AsyncSaveWorker<T>::SaveFn;

    explicit AsyncSaveDispatcher(SaveFn saveFn)
        : m_worker(new AsyncSaveWorker<T>(std::move(saveFn)))
    {
        m_worker->moveToThread(&m_thread);
        m_thread.start();
    }

    ~AsyncSaveDispatcher()
    {
        m_thread.quit();
        m_thread.wait();
        delete m_worker;
    }

    AsyncSaveDispatcher(const AsyncSaveDispatcher&) = delete;
    AsyncSaveDispatcher& operator=(const AsyncSaveDispatcher&) = delete;

    // Queues `value` to be saved on the background thread. Taken by value and copied again into the
    // queued functor below so the worker gets its own independent snapshot rather than touching the
    // caller's live object from another thread -- same reasoning every prior *ViewModel's own
    // dispatch call already had.
    void dispatch(T value) const
    {
        QMetaObject::invokeMethod(
            m_worker, [worker = m_worker, value = std::move(value)]() { worker->save(value); }, Qt::QueuedConnection);
    }

private:
    QThread m_thread;
    AsyncSaveWorker<T>* m_worker = nullptr;
};
