# Authored by this repo's overlay port (not the upstream grammar's own CMakeLists.txt, which
# regenerates src/parser.c from grammar.json via the tree-sitter CLI). The release archive already
# ships a pre-generated src/parser.c, so this just compiles it directly -- Architecture.md §6/§14.100.
# This CMakeLists.txt is copied into and configured from the tree-sitter-markdown/ subdirectory of
# the tree-sitter-grammars/tree-sitter-markdown monorepo (see portfile.cmake), so every path below
# is relative to that subdirectory, not the repo root.
cmake_minimum_required(VERSION 3.20)
project(tree-sitter-markdown LANGUAGES C)

add_library(tree-sitter-markdown STATIC src/parser.c)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
    target_sources(tree-sitter-markdown PRIVATE src/scanner.c)
endif()
target_include_directories(tree-sitter-markdown PRIVATE src)
set_target_properties(tree-sitter-markdown PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
# Unlike every other vendored grammar, this one's header lives under bindings/c/tree-sitter/ (a
# hyphenated subdirectory) rather than bindings/c/ directly or bindings/c/tree_sitter/.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/tree-sitter-markdown.h")
    install(FILES bindings/c/tree-sitter-markdown.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/tree_sitter/tree-sitter-markdown.h")
    install(FILES bindings/c/tree_sitter/tree-sitter-markdown.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
else()
    install(FILES bindings/c/tree-sitter/tree-sitter-markdown.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
endif()
install(TARGETS tree-sitter-markdown
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
