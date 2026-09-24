add_library(explorer_application STATIC
    IFileSystemRepository.h
    IContextMenuProvider.h
    ITagRepository.h
    IMediaDecoder.h
    IGitStatusProvider.h
    IGitBranchProvider.h
    IDirectoryWatcher.h
    IFolderMonitor.h
    FolderChangeEvent.h
    MediaExtensions.h
    ArchiveExtensions.h
    VirtualPaths.h
    SearchCriteria.h
    DriveSpaceInfo.h
    GitFileStatus.h
    GitDirectoryStatus.h
    GitRepositorySummary.h
    FileNavigationUseCase.h
    FileNavigationUseCase.cpp
    TagManagementUseCase.h
    TagManagementUseCase.cpp
    MediaProcessingUseCase.h
    MediaProcessingUseCase.cpp
    FilePreviewUseCase.h
    FilePreviewUseCase.cpp
    GitStatusUseCase.h
    GitStatusUseCase.cpp
    GitBranchUseCase.h
    GitBranchUseCase.cpp
    DirectoryWatchUseCase.h
    DirectoryWatchUseCase.cpp
    FolderMonitorUseCase.h
    FolderMonitorUseCase.cpp
)

target_include_directories(explorer_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_application PUBLIC explorer_domain)
