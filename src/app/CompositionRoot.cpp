#include "CompositionRoot.h"

#include <filesystem>

#include <QStandardPaths>

#include "TabViewModel.h"
#include "WorkspaceController.h"
#include "WorkspacePaneViewModel.h"

namespace
{
    // Architecture.md §9: database lives under the app-data location (%LOCALAPPDATA%/Exp-LORer/
    // on Windows), portable to Linux without code changes via QStandardPaths.
    std::filesystem::path databasePath()
    {
        const auto appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return std::filesystem::path(appDataDir.toStdWString()) / "explorer.db";
    }
}

CompositionRoot::CompositionRoot()
    : m_fileNavigationUseCase(m_fileSystemRepository)
    , m_tagRepository(databasePath())
    , m_tagManagementUseCase(m_tagRepository, m_fileSystemRepository)
{
}

CompositionRoot::~CompositionRoot() = default;

std::unique_ptr<TabViewModel> CompositionRoot::createTabViewModel()
{
    return std::make_unique<TabViewModel>(m_fileNavigationUseCase);
}

std::unique_ptr<WorkspacePaneViewModel> CompositionRoot::createWorkspacePaneViewModel(WorkspacePaneId id)
{
    return std::make_unique<WorkspacePaneViewModel>(m_fileNavigationUseCase, id);
}

std::unique_ptr<WorkspaceController> CompositionRoot::createWorkspaceController()
{
    return std::make_unique<WorkspaceController>(m_fileNavigationUseCase, m_tagManagementUseCase);
}
