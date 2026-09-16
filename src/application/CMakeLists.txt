add_library(explorer_application STATIC
    IFileSystemRepository.h
    IContextMenuProvider.h
    ITagRepository.h
    IMediaDecoder.h
    VirtualPaths.h
    FileNavigationUseCase.h
    FileNavigationUseCase.cpp
    TagManagementUseCase.h
    TagManagementUseCase.cpp
    MediaProcessingUseCase.h
    MediaProcessingUseCase.cpp
)

target_include_directories(explorer_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_application PUBLIC explorer_domain)
