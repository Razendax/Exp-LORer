#pragma once

#include <memory>

#include "FileNavigationUseCase.h"
#include "SQLiteTagRepository.h"
#include "StandardFileSystemRepository.h"
#include "TagManagementUseCase.h"
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

private:
    StandardFileSystemRepository m_fileSystemRepository;
    FileNavigationUseCase m_fileNavigationUseCase;
    SQLiteTagRepository m_tagRepository;
    TagManagementUseCase m_tagManagementUseCase;
};
