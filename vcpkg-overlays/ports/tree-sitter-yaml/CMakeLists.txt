# Authored by this repo's overlay port (not the upstream grammar's own CMakeLists.txt, which
# regenerates src/parser.c from grammar.json via the tree-sitter CLI). The release archive already
# ships a pre-generated src/parser.c, so this just compiles it directly -- Architecture.md
# §6/§14.100. Unlike every other vendored grammar, this one's src/ also ships three schema-variant
# files (schema.core.c/schema.json.c/schema.legacy.c) that parser.c calls into, so all three get
# compiled into the static lib alongside scanner.c.
cmake_minimum_required(VERSION 3.20)
project(tree-sitter-yaml LANGUAGES C)

add_library(tree-sitter-yaml STATIC src/parser.c)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
    target_sources(tree-sitter-yaml PRIVATE src/scanner.c)
endif()
foreach(schema_file IN ITEMS schema.core.c schema.json.c schema.legacy.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${schema_file}")
        target_sources(tree-sitter-yaml PRIVATE "src/${schema_file}")
    endif()
endforeach()
target_include_directories(tree-sitter-yaml PRIVATE src)
set_target_properties(tree-sitter-yaml PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/tree-sitter-yaml.h")
    install(FILES bindings/c/tree-sitter-yaml.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
else()
    install(FILES bindings/c/tree_sitter/tree-sitter-yaml.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tree_sitter")
endif()
install(TARGETS tree-sitter-yaml
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
