# Authored by this repo's overlay port. Only the "typescript" grammar variant is built (not "tsx" --
# out of scope, Architecture.md §14.26). The release archive ships a pre-generated
# typescript/src/parser.c, so this just compiles it directly rather than regenerating via the CLI.
cmake_minimum_required(VERSION 3.20)
project(tree-sitter-typescript LANGUAGES C)

add_library(tree-sitter-typescript STATIC typescript/src/parser.c)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/typescript/src/scanner.c")
    target_sources(tree-sitter-typescript PRIVATE typescript/src/scanner.c)
endif()
target_include_directories(tree-sitter-typescript PRIVATE typescript/src)
set_target_properties(tree-sitter-typescript PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
install(FILES bindings/c/tree-sitter-typescript.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
install(TARGETS tree-sitter-typescript
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
