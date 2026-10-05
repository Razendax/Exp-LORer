add_library(explorer_adapters_filesystem STATIC
    StandardFileSystemRepository.h
    StandardFileSystemRepository.cpp
    DriveLabel.h
    DriveLabel.cpp
    DirectoryWatchRegistry.h
    DirectoryWatchRegistry.cpp
    StandardDirectoryWatcher.h
    StandardDirectoryWatcher.cpp
    StandardFolderMonitor.h
    StandardFolderMonitor.cpp
)

target_include_directories(explorer_adapters_filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_filesystem PUBLIC explorer_application explorer_adapters_archive)

find_package(xxHash CONFIG REQUIRED)
target_link_libraries(explorer_adapters_filesystem PRIVATE xxHash::xxhash)

# StandardDirectoryWatcher is this module's first Qt dependency (QFileSystemWatcher/QObject) --
# StandardFileSystemRepository/DriveLabel stay plain std::filesystem/Win32.
find_package(Qt6 COMPONENTS Core REQUIRED)
target_link_libraries(explorer_adapters_filesystem PRIVATE Qt6::Core)

if(WIN32)
    target_link_libraries(explorer_adapters_filesystem PRIVATE Shell32 Ole32)
endif()

target_link_libraries(explorer_adapters_filesystem PRIVATE explorer_adapters_logging)
