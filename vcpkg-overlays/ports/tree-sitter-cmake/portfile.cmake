vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO uyha/tree-sitter-cmake
    REF "v${VERSION}"
    SHA512 f09833324b6e247c282f5f3903b0b62d4eea545ae509847e370816a14ec975a37175dee9cfb3852c539bbec0b84d3d1afb485a8696c8dc60a7a8f85d3b636215
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
