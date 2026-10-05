add_library(explorer_adapters_duplicates STATIC
    StandardFileHasher.h
    StandardFileHasher.cpp
    StandardDuplicateFileScanner.h
    StandardDuplicateFileScanner.cpp
)

target_include_directories(explorer_adapters_duplicates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_duplicates PUBLIC explorer_application)

find_package(xxHash CONFIG REQUIRED)
target_link_libraries(explorer_adapters_duplicates PRIVATE xxHash::xxhash)
