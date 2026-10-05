add_library(explorer_adapters_fragmentation STATIC
    WindowsFragmentationScanner.h
    WindowsFragmentationScanner.cpp
    SQLiteFragmentationRepository.h
    SQLiteFragmentationRepository.cpp
)

target_include_directories(explorer_adapters_fragmentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_fragmentation PUBLIC explorer_application)

find_package(Qt6 COMPONENTS Sql REQUIRED)
target_link_libraries(explorer_adapters_fragmentation PRIVATE Qt6::Sql)
