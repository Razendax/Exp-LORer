vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-json
    REF "v${VERSION}"
    SHA512 0027c5d85498575bb10cfe739023b27b19e730be1921c52ef141948ad0d003e5318c8fa3a3440af86c53affa236834fa200cbf09790f0b85e5cdc264ad3e2f3e
    HEAD_REF master
)

# Overlay our own CMakeLists.txt (Architecture.md §6/§14.26) -- compiles the archive's already
# pre-generated src/parser.c directly, instead of the upstream repo's own CMakeLists.txt which
# regenerates parser.c via the tree-sitter CLI (an extra host toolchain this repo avoids).
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
