#include "DirectoryWatchService.h"

#include <algorithm>

#include <QTimer>

#include "TabViewModel.h"

namespace
{
    // A burst of native change events (e.g. a multi-file copy) coalesces into one refresh.
    constexpr int kDebounceMs = 400;
}

DirectoryWatchService::DirectoryWatchService(DirectoryWatchUseCase& useCase, QObject* parent)
    : QObject(parent)
    , m_useCase(useCase)
{
}

DirectoryWatchService::~DirectoryWatchService()
{
    for (const WatchedTab& watched : m_watched)
    {
        m_useCase.unwatch(watched.watchId);
    }
}

void DirectoryWatchService::setVisibleTabs(const std::vector<TabViewModel*>& tabs)
{
    for (auto it = m_watched.begin(); it != m_watched.end();)
    {
        const bool stillVisible = std::find(tabs.begin(), tabs.end(), it->tab) != tabs.end();
        if (stillVisible)
        {
            ++it;
            continue;
        }

        m_useCase.unwatch(it->watchId);
        delete it->debounceTimer;
        it = m_watched.erase(it);
    }

    for (TabViewModel* tab : tabs)
    {
        if (tab == nullptr)
        {
            continue;
        }

        const bool alreadyWatched =
            std::any_of(m_watched.begin(), m_watched.end(), [tab](const WatchedTab& watched) { return watched.tab == tab; });
        if (alreadyWatched)
        {
            continue;
        }

        startWatching(tab);
    }
}

void DirectoryWatchService::startWatching(TabViewModel* tab)
{
    WatchedTab watched;
    watched.tab = tab;
    watched.debounceTimer = new QTimer(this);
    watched.debounceTimer->setSingleShot(true);
    watched.debounceTimer->setInterval(kDebounceMs);

    connect(watched.debounceTimer, &QTimer::timeout, tab, [tab]() { tab->refresh(); });

    QTimer* debounceTimer = watched.debounceTimer;
    auto result = m_useCase.watch(tab->currentPath(), [debounceTimer]() { debounceTimer->start(); });
    watched.watchId = result ? result.value() : DirectoryWatchId{ 0 };

    connect(tab, &TabViewModel::currentPathChanged, this, [this, tab](const std::filesystem::path&) { rewatch(tab); });
    connect(tab, &QObject::destroyed, this, [this](QObject* tabObject) { handleTabDestroyed(tabObject); });

    m_watched.push_back(watched);

    // Catch-up-on-becoming-visible: pick up anything that changed while this tab wasn't watched
    // (a different tab was active, or the pane wasn't visible under the layout). Unconditional --
    // even when the OS-level watch itself failed (e.g. "This PC" or an archive-virtual path),
    // there's still a one-time freshness benefit and no ongoing live-update to skip.
    tab->refresh();
}

void DirectoryWatchService::rewatch(TabViewModel* tab)
{
    const auto it = std::find_if(m_watched.begin(), m_watched.end(), [tab](const WatchedTab& watched) { return watched.tab == tab; });
    if (it == m_watched.end())
    {
        return;
    }

    m_useCase.unwatch(it->watchId);

    QTimer* debounceTimer = it->debounceTimer;
    auto result = m_useCase.watch(tab->currentPath(), [debounceTimer]() { debounceTimer->start(); });
    it->watchId = result ? result.value() : DirectoryWatchId{ 0 };
}

void DirectoryWatchService::handleTabDestroyed(QObject* tabObject)
{
    const auto it =
        std::find_if(m_watched.begin(), m_watched.end(), [tabObject](const WatchedTab& watched) { return watched.tab == tabObject; });
    if (it == m_watched.end())
    {
        return;
    }

    m_useCase.unwatch(it->watchId);
    delete it->debounceTimer;
    m_watched.erase(it);
}
