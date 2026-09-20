vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-javascript
    REF "v${VERSION}"
    SHA512 79ecde111ddd397c4717500dca58e30f14848433d7c546443ae40164b057fd1cfc43d89f98d12a63ecd464d2a5e6cdb49b0e6cb161eb37fadbf622822fb19a29
    HEAD_REF master
)

# Overlay our own CMakeLists.txt (Architecture.md §6/§14.26) -- compiles the archive's already
# pre-generated src/parser.c/scanner.c directly, instead of the upstream repo's own CMakeLists.txt
# which regenerates parser.c via the tree-sitter CLI (an extra host toolchain this repo avoids).
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
