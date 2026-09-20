add_library(explorer_adapters_media STATIC
    ThumbnailCache.h
    ThumbnailCache.cpp
    CachingMediaDecoder.h
    CachingMediaDecoder.cpp
    VipsImageDecoder.h
    VipsImageDecoder.cpp
    FFmpegMediaDecoder.h
    FFmpegMediaDecoder.cpp
    MediaDecoder.h
    MediaDecoder.cpp
)

target_include_directories(explorer_adapters_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_media PUBLIC explorer_application)

find_package(xxHash CONFIG REQUIRED)
target_link_libraries(explorer_adapters_media PRIVATE xxHash::xxhash)

# libvips and FFmpeg are pkgconfig-only vcpkg ports (no CMake config files) -- use vcpkg's own
# pkgconf tool, pointed at this triplet's installed .pc files, rather than relying on a
# system-installed pkg-config (which may not exist on a fresh Windows machine). pkgconf is a host
# dependency (vcpkg.json), so it lands under whichever triplet vcpkg auto-detected as the host --
# not necessarily VCPKG_TARGET_TRIPLET, and VCPKG_HOST_TRIPLET is only set when a preset overrides
# it (this project's presets don't) -- so locate it by globbing instead of assuming either.
if(NOT PKG_CONFIG_EXECUTABLE)
    file(GLOB Z_EXP_LORER_PKGCONF_CANDIDATES "${VCPKG_INSTALLED_DIR}/*/tools/pkgconf/pkgconf.exe")
    if(Z_EXP_LORER_PKGCONF_CANDIDATES)
        list(GET Z_EXP_LORER_PKGCONF_CANDIDATES 0 Z_EXP_LORER_PKGCONF_EXECUTABLE)
        set(PKG_CONFIG_EXECUTABLE "${Z_EXP_LORER_PKGCONF_EXECUTABLE}" CACHE FILEPATH "Path to pkgconf" FORCE)
    endif()
    unset(Z_EXP_LORER_PKGCONF_CANDIDATES)
    unset(Z_EXP_LORER_PKGCONF_EXECUTABLE)
endif()
set(ENV{PKG_CONFIG_PATH} "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")

find_package(PkgConfig REQUIRED)

pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips)
target_link_libraries(explorer_adapters_media PRIVATE PkgConfig::VIPS)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
target_link_libraries(explorer_adapters_media PRIVATE PkgConfig::FFMPEG)
