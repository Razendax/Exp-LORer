add_library(explorer_adapters_terminal STATIC
    AnsiTerminalBuffer.h
    AnsiTerminalBuffer.cpp
    WindowsConPtyProcess.h
    WindowsConPtyProcess.cpp
)

target_include_directories(explorer_adapters_terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# QColor (used by AnsiTerminalBuffer::Cell) lives in Qt6::Gui, not Qt6::Core.
target_link_libraries(explorer_adapters_terminal PUBLIC Qt6::Core Qt6::Gui explorer_domain)
