vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wharflab/tree-sitter-powershell
    REF "v${VERSION}"
    SHA512 38d780e58fb7ab3c7fde363b887268424a10ace7783dc20277d5bbb09d105048a8dd44acc0801c62880f07ab7ea9d63fe382cae602d1f26b37c86c63d3fd8458
    HEAD_REF main
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
