vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter-grammars/tree-sitter-yaml
    REF "v${VERSION}"
    SHA512 ad91f349db724af9feecb909a8f7b3eb0421b45432636f399d94ec48c9e0d7ece709d416c20949d2439c9de7aadf3d53762e2a74a97cb9115a94de81ce0914bf
    HEAD_REF master
)

# Overlay our own CMakeLists.txt (Architecture.md §6/§14.100) -- compiles the archive's already
# pre-generated src/parser.c/scanner.c directly, instead of the upstream repo's own CMakeLists.txt
# which regenerates parser.c via the tree-sitter CLI (an extra host toolchain this repo avoids).
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
