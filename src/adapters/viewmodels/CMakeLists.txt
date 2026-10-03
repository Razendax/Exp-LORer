add_library(explorer_adapters_viewmodels STATIC
    NavigationHistory.h
    NavigationHistory.cpp
    TabViewModel.h
    TabViewModel.cpp
    ViewMode.h
    FileListModel.h
    FileListModel.cpp
    FileSizeFormat.h
    SplitLayout.h
    WorkspacePaneId.h
    WorkspaceConfig.h
    WorkspaceLayoutTopology.h
    WorkspaceLayoutTopology.cpp
    WorkspacePaneViewModel.h
    WorkspacePaneViewModel.cpp
    WorkspaceController.h
    WorkspaceController.cpp
    TagColorPalette.h
    TagListViewModel.h
    TagListViewModel.cpp
    TagManagerViewModel.h
    TagManagerViewModel.cpp
    TagChangeNotifier.h
    TagChangeNotifier.cpp
    FileOperationsController.h
    FileOperationsController.cpp
    FilePreviewWorker.h
    FilePreviewWorker.cpp
    FilePreviewViewModel.h
    FilePreviewViewModel.cpp
    DriveSpaceInfoWorker.h
    DriveSpaceInfoWorker.cpp
    GitStatusWorker.h
    GitStatusWorker.cpp
    GitStatusService.h
    GitStatusService.cpp
    FileTagsWorker.h
    FileTagsWorker.cpp
    FileTagsService.h
    FileTagsService.cpp
    AdvancedSearchWorker.h
    AdvancedSearchWorker.cpp
    AdvancedSearchService.h
    AdvancedSearchService.cpp
    FileTransferWorker.h
    FileTransferWorker.cpp
    FileTransferService.h
    FileTransferService.cpp
    DirectoryWatchService.h
    DirectoryWatchService.cpp
    MonitoringPanelViewModel.h
    MonitoringPanelViewModel.cpp
    DiskUsageWorker.h
    DiskUsageWorker.cpp
    DiskUsageService.h
    DiskUsageService.cpp
    FolderSizeViewModel.h
    FolderSizeViewModel.cpp
    FolderTreeModel.h
    FolderTreeModel.cpp
    SearchResultsTreeModel.h
    SearchResultsTreeModel.cpp
)

target_include_directories(explorer_adapters_viewmodels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# explorer_adapters_git: AdvancedSearchWorker constructs its own LibGit2StatusProvider/
# GitStatusUseCase pair directly (Architecture.md §14.45), the same "own thread-affine instance"
# precedent its own SQLiteTagRepository connection already set -- this is the one place the
# viewmodels layer reaches past IGitStatusProvider into a concrete git adapter.
target_link_libraries(explorer_adapters_viewmodels PUBLIC explorer_application explorer_adapters_decoration explorer_adapters_notifications explorer_adapters_persistence explorer_adapters_diskusage explorer_adapters_git Qt6::Core Qt6::Widgets)
