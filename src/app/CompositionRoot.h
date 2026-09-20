#pragma once

#include <memory>

#include "AppConfigStore.h"
#include "CachingMediaDecoder.h"
#include "FileNavigationUseCase.h"
#include "FilePreviewUseCase.h"
#include "MediaDecoder.h"
#include "SQLiteTagRepository.h"
#include "ShellContextMenuProvider.h"
#include "StandardFileSystemRepository.h"
#include "TagManagementUseCase.h"
#include "ThumbnailCache.h"
#include "WorkspacePaneId.h"

class TabViewModel;
class WorkspacePaneViewModel;
class WorkspaceController;

// Wires concrete adapters/repositories into use cases via constructor injection. Owns the shared
// adapter/use-case instances; ViewModels are created only through its factory methods
// (Architecture.md §14.6).
class CompositionRoot
{
public:
    CompositionRoot();
    ~CompositionRoot();

    std::unique_ptr<TabViewModel> createTabViewModel();
    std::unique_ptr<WorkspacePaneViewModel> createWorkspacePaneViewModel(WorkspacePaneId id);
    std::unique_ptr<WorkspaceController> createWorkspaceController();

    AppConfigStore& appConfigStore() noexcept { return m_appConfigStore; }
    TagManagementUseCase& tagManagementUseCase() noexcept { return m_tagManagementUseCase; }

private:
    StandardFileSystemRepository m_fileSystemRepository;
    ShellContextMenuProvider m_contextMenuProvider;
    FileNavigationUseCase m_fileNavigationUseCase;
    SQLiteTagRepository m_tagRepository;
    TagManagementUseCase m_tagManagementUseCase;
    AppConfigStore m_appConfigStore;

    MediaDecoder m_mediaDecoder;
    ThumbnailCache m_thumbnailCache;
    CachingMediaDecoder m_cachingMediaDecoder;
    FilePreviewUseCase m_filePreviewUseCase;
};
