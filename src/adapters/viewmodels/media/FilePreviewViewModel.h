#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <optional>
#include <utility>

#include <QObject>
#include <QString>
#include <QThread>

#include "FileNode.h"
#include "FilePreview.h"
#include "Result.h"

class FilePreviewUseCase;
class IFileSystemRepository;
class TabViewModel;
class FilePreviewWorker;

// The single shared ViewModel behind the right panel's Preview tab (Architecture.md §14.25),
// retargeted by WorkspaceController to whichever pane/tab currently has focus -- same posture as
// TagListViewModel (§14.9). Owns one dedicated background QThread + FilePreviewWorker (one
// always-alive worker, not a pool -- only one active preview is ever needed at a time), the same
// "one background thread, results marshaled back via a queued call" shape WindowsConPtyProcess
// already established (§14.23). Untested per the codebase's existing UI/ViewModel convention
// (§11, same as WorkspaceController/TagListViewModel).
class FilePreviewViewModel : public QObject
{
    Q_OBJECT

public:
    FilePreviewViewModel(FilePreviewUseCase& filePreviewUseCase, IFileSystemRepository& fileSystemRepository,
                          QObject* parent = nullptr);
    ~FilePreviewViewModel() override;

public slots:
    // Called by WorkspaceController's retargeting hook, mirroring TagListViewModel::setActiveTab.
    void setActiveTab(TabViewModel* tab);

    // Called by PreviewPanelWidget from showEvent/hideEvent. Background generation is skipped
    // while the panel isn't the visible tab; reactivating catches up immediately if the target
    // changed while hidden.
    void setPanelActive(bool active);

signals:
    void previewLoading();
    void previewReady(const FilePreview& preview);
    void previewFailed(const QString& message);

    // No resolvable target (e.g. multi-selection) -- mirrors the tag panel's "no target" case.
    void previewCleared();

private:
    struct CacheKey
    {
        std::filesystem::path path;
        std::chrono::system_clock::time_point mtime;

        bool operator==(const CacheKey& other) const noexcept { return path == other.path && mtime == other.mtime; }
    };

    void onActiveTabStateChanged();
    std::optional<FileNode> resolveTarget() const;

    // Re-resolves the target and either serves a cached Folder/Text result, or bumps the
    // generation and dispatches a new background generation. No-op (deferred) while the panel
    // isn't active.
    void refresh();

    void dispatchGeneration(const FileNode& target);
    void onWorkerFinished(int generation, Result<FilePreview> result);

    std::optional<FilePreview> lookupSmallResultCache(const CacheKey& key);
    void insertSmallResultCache(const CacheKey& key, const FilePreview& preview);

    FilePreviewUseCase& m_filePreviewUseCase;
    IFileSystemRepository& m_fileSystemRepository;
    TabViewModel* m_activeTab = nullptr;

    QThread m_workerThread;
    FilePreviewWorker* m_worker = nullptr;

    int m_generation = 0;
    bool m_panelActive = false;
    bool m_pendingRefreshWhileInactive = false;

    // Folder/Text results only -- Image/Video are already cached on disk by CachingMediaDecoder,
    // so caching them here too would be redundant. front = most recently used.
    std::deque<std::pair<CacheKey, FilePreview>> m_smallResultCache;
    static constexpr std::size_t kSmallResultCacheCapacity = 20;

    static constexpr int kMaxImageWidth = 512;
    static constexpr int kMaxImageHeight = 512;
    static constexpr std::size_t kMaxTextBytes = 64 * 1024;
};
