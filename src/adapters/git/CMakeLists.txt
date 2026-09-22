add_library(explorer_adapters_git STATIC
    LibGit2Repository.h
    LibGit2Repository.cpp
    LibGit2StatusProvider.h
    LibGit2StatusProvider.cpp
)

target_include_directories(explorer_adapters_git PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_git PUBLIC explorer_application)

find_package(libgit2 CONFIG REQUIRED)
# PUBLIC (not PRIVATE like media's vips/FFmpeg) because LibGit2StatusProviderTest initializes its
# fixture repos directly via libgit2's own git_repository_init C API, which needs libgit2's
# headers/link dependency to come along transitively -- same reasoning as archive's PUBLIC link.
# vcpkg's libgit2 port exports the target as libgit2::libgit2package (verified against the
# installed port's generated libgit2Targets.cmake), not the more predictable libgit2::libgit2.
target_link_libraries(explorer_adapters_git PUBLIC libgit2::libgit2package)
