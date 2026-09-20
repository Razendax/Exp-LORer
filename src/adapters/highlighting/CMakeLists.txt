add_library(explorer_adapters_highlighting STATIC
    Language.h
    LanguageRegistry.h
    LanguageRegistry.cpp
    SyntaxHighlightEngine.h
    SyntaxHighlightEngine.cpp
    "${CMAKE_SOURCE_DIR}/resources/tree-sitter-queries.qrc"
)

target_include_directories(explorer_adapters_highlighting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Core is linked only for the one resource-loading function in SyntaxHighlightEngine.cpp that
# reads a language's bundled highlights.scm out of Qt resources (Architecture.md §14.26) -- this
# module otherwise never touches Qt.
target_link_libraries(explorer_adapters_highlighting PUBLIC Qt6::Core)

find_package(unofficial-tree-sitter CONFIG REQUIRED)
target_link_libraries(explorer_adapters_highlighting PRIVATE unofficial::tree-sitter::tree-sitter)

# The per-language grammars (Architecture.md §6/§14.26) are vendored via vcpkg overlay ports
# (vcpkg-overlays/ports/tree-sitter-<lang>, except tree-sitter-c which the public vcpkg registry
# already ships) as plain static libraries with no CMake package config, so they're located the
# same way vcpkg itself expects a config-less port to be consumed: find_library against the
# triplet's installed lib directory (which the vcpkg toolchain file already adds to the search path).
foreach(grammar IN ITEMS c cpp c-sharp python javascript typescript json html css)
    find_library(EXP_LORER_TS_${grammar}_LIB NAMES tree-sitter-${grammar} REQUIRED)
    target_link_libraries(explorer_adapters_highlighting PRIVATE ${EXP_LORER_TS_${grammar}_LIB})
endforeach()
