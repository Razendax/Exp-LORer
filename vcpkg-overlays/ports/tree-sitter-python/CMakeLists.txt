# Authored by this repo's overlay port (not the upstream grammar's own CMakeLists.txt, which
# regenerates src/parser.c from grammar.json via the tree-sitter CLI). The release archive already
# ships a pre-generated src/parser.c, so this just compiles it directly -- Architecture.md §6/§14.26.
cmake_minimum_required(VERSION 3.20)
project(tree-sitter-python LANGUAGES C)

add_library(tree-sitter-python STATIC src/parser.c)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
    target_sources(tree-sitter-python PRIVATE src/scanner.c)
endif()
target_include_directories(tree-sitter-python PRIVATE src)
set_target_properties(tree-sitter-python PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/tree-sitter-python.h")
    install(FILES bindings/c/tree-sitter-python.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
else()
    install(FILES bindings/c/tree_sitter/tree-sitter-python.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
endif()
install(TARGETS tree-sitter-python
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
