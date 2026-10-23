# Local Windows installer packaging (docs/features/09-app-infrastructure.md). Builds a per-user
# NSIS installer via `cpack` -- see build.ps1 -Package. Not wired into CI; local-only for now.

if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

if(NOT WIN32)
    return()
endif()

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "Exp-LORer")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Exp-LORer")
set(CPACK_PACKAGE_VENDOR "Exp-LORer")
set(CPACK_PACKAGE_CONTACT "ravilsafarov2013@gmail.com")

# Per-user install, no admin/UAC prompt: $LOCALAPPDATA\Programs\Exp-LORer, matching the patched
# NSIS.template.in below (RequestExecutionLevel user).
set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\\\Programs")
set(CPACK_NSIS_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/NSIS.template.in")

set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_PACKAGE_EXECUTABLES "Exp-LORer" "Exp-LORer")
set(CPACK_NSIS_DISPLAY_NAME "Exp-LORer")
set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
set(CPACK_NSIS_MODIFY_PATH OFF)

set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/src/app/resources/AppIcon.ico")
set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/src/app/resources/AppIcon.ico")

# CPACK_PACKAGE_VERSION* defaults from the project(... VERSION ...) call in the root CMakeLists.txt.

include(CPack)
