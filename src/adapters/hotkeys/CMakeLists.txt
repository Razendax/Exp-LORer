add_library(explorer_adapters_hotkeys STATIC
    HotkeyAction.h
    HotkeyChord.h
    HotkeyCatalogEntry.h
    HotkeyCatalog.h
    HotkeyCatalog.cpp
    HotkeyBindings.h
    HotkeyBindings.cpp
)

target_include_directories(explorer_adapters_hotkeys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# HotkeyChord/HotkeyCatalogEntry hold QKeySequence/QString members directly (Architecture.md
# §14.x), unlike Favorites' plain std::filesystem::path/std::string -- Qt6::Gui is needed for
# QKeySequence (QKeyEvent/QKeyCombination), Qt6::Core for QString.
target_link_libraries(explorer_adapters_hotkeys PUBLIC explorer_domain Qt6::Core Qt6::Gui)
