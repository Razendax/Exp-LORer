#include "FilePreviewViewModel.h"

#include <algorithm>
#include <utility>

#include <QMetaObject>

#include "FilePreviewUseCase.h"
#include "FilePreviewWorker.h"
#include "IFileSystemRepository.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"

FilePreviewViewModel::FilePreviewViewModel(FilePreviewUseCase& filePreviewUseCase, IFileSystemRepository& fileSystemRepository,
                                            QObject* parent)
    : QObject(parent)
    , m_filePreviewUseCase(filePreviewUseCase)
    , m_fileSystemRepository(fileSystemRepository)
{
    m_worker = new FilePreviewWorker(m_filePreviewUseCase);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

FilePreviewViewModel::~FilePreviewViewModel()
{
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void FilePreviewViewModel::setActiveTab(TabViewModel* tab)
{
    if (m_activeTab == tab)
    {
        return;
    }

    if (m_activeTab)
    {
        disconnect(m_activeTab, &TabViewModel::currentPathChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
        disconnect(m_activeTab, &TabViewModel::selectedEntriesChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
        disconnect(m_activeTab, &TabViewModel::directoryContentsChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
    }

    m_activeTab = tab;

    if (m_activeTab)
    {
        connect(m_activeTab, &TabViewModel::currentPathChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
        connect(m_activeTab, &TabViewModel::selectedEntriesChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
        connect(m_activeTab, &TabViewModel::directoryContentsChanged, this, &FilePreviewViewModel::onActiveTabStateChanged);
    }

    refresh();
}

void FilePreviewViewModel::setPanelActive(bool active)
{
    if (m_panelActive == active)
    {
        return;
    }

    m_panelActive = active;

    if (m_panelActive && m_pendingRefreshWhileInactive)
    {
        m_pendingRefreshWhileInactive = false;
        refresh();
    }
}

void FilePreviewViewModel::onActiveTabStateChanged()
{
    refresh();
}

std::optional<FileNode> FilePreviewViewModel::resolveTarget() const
{
    if (!m_activeTab)
    {
        return std::nullopt;
    }

    const std::vector<FileNode>& selected = m_activeTab->selectedEntries();
    if (selected.size() == 1)
    {
        return selected.front();
    }
    if (selected.size() > 1)
    {
        return std::nullopt;
    }

    if (m_activeTab->currentPath() == VirtualPaths::ThisPC)
    {
        return std::nullopt;
    }

    auto stat = m_fileSystemRepository.stat(m_activeTab->currentPath());
    if (!stat)
    {
        return std::nullopt;
    }

    return stat.value();
}

void FilePreviewViewModel::refresh()
{
    if (!m_panelActive)
    {
        m_pendingRefreshWhileInactive = true;
        return;
    }

    auto target = resolveTarget();
    ++m_generation;

    if (!target)
    {
        emit previewCleared();
        return;
    }

    const CacheKey key{ target->path(), target->modificationDate() };
    if (auto cached = lookupSmallResultCache(key))
    {
        emit previewReady(*cached);
        return;
    }

    emit previewLoading();
    dispatchGeneration(*target);
}

void FilePreviewViewModel::dispatchGeneration(const FileNode& target)
{
    const int generation = m_generation;
    const int maxImageWidth = kMaxImageWidth;
    const int maxImageHeight = kMaxImageHeight;
    const std::size_t maxTextBytes = kMaxTextBytes;

    // Both hops here are queued functor calls (QMetaObject::invokeMethod), not Qt signals/slots --
    // FileNode/FilePreview/Result aren't default-constructible, so routing them through Qt's
    // meta-type system for cross-thread delivery would be an avoidable risk (see
    // FilePreviewWorker.h). A queued functor just moves its captured state across threads as a
    // plain C++ closure.
    QMetaObject::invokeMethod(
        m_worker,
        [this, generation, target, maxImageWidth, maxImageHeight, maxTextBytes]() {
            Result<FilePreview> result = m_worker->generate(target, maxImageWidth, maxImageHeight, maxTextBytes);

            QMetaObject::invokeMethod(
                this, [this, generation, result = std::move(result)]() mutable { onWorkerFinished(generation, std::move(result)); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void FilePreviewViewModel::onWorkerFinished(int generation, Result<FilePreview> result)
{
    if (generation != m_generation)
    {
        // A newer selection has already superseded this request -- the underlying libvips/FFmpeg
        // call still ran to completion (neither is interruptible mid-call), but its result is
        // simply discarded now (Architecture.md §5/§14.25).
        return;
    }

    if (!result)
    {
        emit previewFailed(QString::fromStdString(result.error().message));
        return;
    }

    FilePreview preview = std::move(result).value();

    if (preview.kind() == FilePreviewKind::Folder || preview.kind() == FilePreviewKind::Text)
    {
        if (auto target = resolveTarget())
        {
            insertSmallResultCache(CacheKey{ target->path(), target->modificationDate() }, preview);
        }
    }

    emit previewReady(preview);
}

std::optional<FilePreview> FilePreviewViewModel::lookupSmallResultCache(const CacheKey& key)
{
    auto it = std::find_if(m_smallResultCache.begin(), m_smallResultCache.end(),
                            [&key](const auto& entry) { return entry.first == key; });
    if (it == m_smallResultCache.end())
    {
        return std::nullopt;
    }

    FilePreview preview = it->second;
    m_smallResultCache.erase(it);
    m_smallResultCache.push_front({ key, preview });
    return preview;
}

void FilePreviewViewModel::insertSmallResultCache(const CacheKey& key, const FilePreview& preview)
{
    m_smallResultCache.erase(std::remove_if(m_smallResultCache.begin(), m_smallResultCache.end(),
                                             [&key](const auto& entry) { return entry.first == key; }),
                              m_smallResultCache.end());

    m_smallResultCache.push_front({ key, preview });
    if (m_smallResultCache.size() > kSmallResultCacheCapacity)
    {
        m_smallResultCache.pop_back();
    }
}
