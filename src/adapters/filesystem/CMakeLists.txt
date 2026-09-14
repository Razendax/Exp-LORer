add_library(explorer_adapters_filesystem STATIC
    StandardFileSystemRepository.h
    StandardFileSystemRepository.cpp
)

target_include_directories(explorer_adapters_filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_filesystem PUBLIC explorer_application)

find_package(xxHash CONFIG REQUIRED)
target_link_libraries(explorer_adapters_filesystem PRIVATE xxHash::xxhash)

if(WIN32)
    target_link_libraries(explorer_adapters_filesystem PRIVATE Shell32)
endif()
