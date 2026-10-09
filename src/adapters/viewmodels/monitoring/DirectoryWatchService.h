#pragma once

#include <vector>

#include <QObject>

#include "DirectoryWatchUseCase.h"

class QTimer;
class TabViewModel;

// Watches the currently-visible+active tabs' directories for external changes and triggers
// TabViewModel::refresh() on a debounced notification (Architecture.md §14.x). Owned long-lived by
// WorkspaceController -- see its own doc comment for why it builds owned services directly rather
// than going through CompositionRoot. No background thread: directory-change notifications arrive
// through Qt's own event loop on this object's (UI) thread already (see StandardDirectoryWatcher).
class DirectoryWatchService : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryWatchService(DirectoryWatchUseCase& useCase, QObject* parent = nullptr);
    ~DirectoryWatchService() override;

public slots:
    // Diffs `tabs` (the currently visible+active tab set) against what's currently watched:
    // newly-visible tabs are watched and refreshed once immediately to catch up on any change
    // missed while unwatched; no-longer-visible tabs are unwatched. Called by WorkspaceController
    // whenever the visible+active tab set may have changed (layout change, any pane's
    // activeTabChanged).
    void setVisibleTabs(const std::vector<TabViewModel*>& tabs);

private:
    struct WatchedTab
    {
        TabViewModel* tab = nullptr;
        DirectoryWatchId watchId = 0;
        QTimer* debounceTimer = nullptr;
    };

    void startWatching(TabViewModel* tab);
    void rewatch(TabViewModel* tab);
    void handleTabDestroyed(QObject* tabObject);

    DirectoryWatchUseCase& m_useCase;
    std::vector<WatchedTab> m_watched;
};
