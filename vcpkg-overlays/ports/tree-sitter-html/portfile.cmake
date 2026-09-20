vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO tree-sitter/tree-sitter-html
    REF "v${VERSION}"
    SHA512 71b8eb2907d372c55a3a28f1d4323fe86b7fcdc028e89ba471bbe49b3b3ca77cb84c9ef41543db44d24dc824625ec2da9767894267104c4386071334023b0f72
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
