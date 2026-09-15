add_library(explorer_adapters_shell STATIC
    ShellContextMenuProvider.h
    ShellContextMenuProvider.cpp
)

target_include_directories(explorer_adapters_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_shell PUBLIC explorer_application)

if(WIN32)
    target_link_libraries(explorer_adapters_shell PRIVATE Shell32 Ole32 Qt6::Core)
endif()
