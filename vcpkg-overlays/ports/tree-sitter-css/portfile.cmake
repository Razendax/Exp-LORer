vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-css
    REF "v${VERSION}"
    SHA512 2cfdcb1c0d7fed9fdd56afa3a290f7164ffe663400bd446caba0d6786eb3bdbdbc31bc6a35a464fbf3bd6241049510d7a57d338d36a98cc725a5b4f0f2a803fd
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
