# Authored by this repo's overlay port (not the upstream grammar's own CMakeLists.txt, which
# regenerates src/parser.c from grammar.json via the tree-sitter CLI). The release archive already
# ships a pre-generated src/parser.c, so this just compiles it directly -- Architecture.md §6/§14.26.
cmake_minimum_required(VERSION 3.20)
project(tree-sitter-css LANGUAGES C)

add_library(tree-sitter-css STATIC src/parser.c)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
    target_sources(tree-sitter-css PRIVATE src/scanner.c)
endif()
target_include_directories(tree-sitter-css PRIVATE src)
set_target_properties(tree-sitter-css PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/tree-sitter-css.h")
    install(FILES bindings/c/tree-sitter-css.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
else()
    install(FILES bindings/c/tree_sitter/tree-sitter-css.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
endif()
install(TARGETS tree-sitter-css
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
