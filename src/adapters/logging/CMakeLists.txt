add_library(explorer_adapters_logging STATIC
    Logging.h
    Logging.cpp
)

target_include_directories(explorer_adapters_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Header-only spdlog: the vcpkg-compiled spdlog::spdlog library is built without
# SPDLOG_WCHAR_FILENAMES, so linking against it would leave the wide-filename constructor of
# rotating_file_sink unresolved. Building it as a header locally (SPDLOG_HEADER_ONLY) lets this
# translation unit instantiate that constructor itself with SPDLOG_WCHAR_FILENAMES. spdlog is used
# nowhere else in the codebase (see Logging.h), so this doesn't create an ODR mismatch elsewhere.
target_link_libraries(explorer_adapters_logging PUBLIC spdlog::spdlog_header_only)
target_compile_definitions(explorer_adapters_logging PRIVATE
    SPDLOG_HEADER_ONLY
    $<$<PLATFORM_ID:Windows>:SPDLOG_WCHAR_FILENAMES>
)
