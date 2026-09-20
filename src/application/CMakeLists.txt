add_library(explorer_application STATIC
    IFileSystemRepository.h
    IContextMenuProvider.h
    ITagRepository.h
    IMediaDecoder.h
    MediaExtensions.h
    VirtualPaths.h
    SearchCriteria.h
    FileNavigationUseCase.h
    FileNavigationUseCase.cpp
    TagManagementUseCase.h
    TagManagementUseCase.cpp
    MediaProcessingUseCase.h
    MediaProcessingUseCase.cpp
    FilePreviewUseCase.h
    FilePreviewUseCase.cpp
)

target_include_directories(explorer_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_application PUBLIC explorer_domain)
