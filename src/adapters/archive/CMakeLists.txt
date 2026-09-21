add_library(explorer_adapters_archive STATIC
    ArchivePathResolver.h
    ArchivePathResolver.cpp
    LibArchiveReader.h
    LibArchiveReader.cpp
    ArchiveIndexCache.h
    ArchiveIndexCache.cpp
)

target_include_directories(explorer_adapters_archive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_archive PUBLIC explorer_application)

find_package(LibArchive REQUIRED)
# PUBLIC (not PRIVATE like media's vips/FFmpeg) because ArchiveFixture.h -- the test-only helper
# that authors zip fixtures via libarchive's own write API -- is included directly by test .cpp
# files, which need libarchive's headers/link dependency to come along transitively.
target_link_libraries(explorer_adapters_archive PUBLIC LibArchive::LibArchive)
