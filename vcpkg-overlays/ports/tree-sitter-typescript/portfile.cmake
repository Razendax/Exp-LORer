vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-typescript
    REF "v${VERSION}"
    SHA512 f91d49e9af3f714fe3c8c442f6d1abd12a7b8d65b5e13f536e95132127b7a4840e1d7578780e537929be18c9472f87bd2f9ec2e9f7a41cf739231134965aeb02
    HEAD_REF master
)

# Overlay our own CMakeLists.txt (Architecture.md §6/§14.26) -- builds only the "typescript" grammar
# variant (not "tsx", out of scope) directly from the archive's pre-generated
# typescript/src/parser.c, instead of the upstream repo's own CMakeLists.txt which regenerates it
# via the tree-sitter CLI (an extra host toolchain this repo avoids).
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
