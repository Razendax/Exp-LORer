add_library(explorer_adapters_diskusage STATIC
    WindowsDiskUsageScanner.h
    WindowsDiskUsageScanner.cpp
    SQLiteDiskUsageRepository.h
    SQLiteDiskUsageRepository.cpp
)

target_include_directories(explorer_adapters_diskusage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_diskusage PUBLIC explorer_application)

find_package(Qt6 COMPONENTS Sql REQUIRED)
target_link_libraries(explorer_adapters_diskusage PRIVATE Qt6::Sql)
