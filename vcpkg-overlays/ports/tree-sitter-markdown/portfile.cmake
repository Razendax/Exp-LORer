vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter-grammars/tree-sitter-markdown
    REF "v${VERSION}"
    SHA512 d88fb4af8248fc2befe0bdc69d4ad8b3b4bc90e2114a7c8e597c9ccc189f02d776ad8fbf989578eb0ab73c3410dde41732bce6cff2b5614511883439cf67dda5
    HEAD_REF split_parser
)

# Overlay our own CMakeLists.txt (Architecture.md §6/§14.100) -- builds only the block grammar
# (tree-sitter-markdown/, not the sibling tree-sitter-markdown-inline/ subdirectory -- out of
# scope, see §14.100's "Not built") directly from that subdirectory's already pre-generated
# src/parser.c/scanner.c, instead of the upstream repo's own CMakeLists.txt which regenerates them
# via the tree-sitter CLI (an extra host toolchain this repo avoids). Unlike every other vendored
# grammar's single-grammar repo, this one is a monorepo, so -- the one deviation from the standard
# portfile template -- the overlay CMakeLists.txt is copied into and configured from the
# tree-sitter-markdown/ subdirectory rather than ${SOURCE_PATH} itself.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/tree-sitter-markdown")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/tree-sitter-markdown"
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
