add_library(explorer_adapters_config STATIC
    AppConfig.h
    JsonFileStore.h
    JsonFileStore.cpp
    AppConfigStore.h
    AppConfigStore.cpp
    HighlightTheme.h
    HighlightTheme.cpp
    HighlightThemeStore.h
    HighlightThemeStore.cpp
    HighlightThemeSaveWorker.h
    HighlightThemeSaveWorker.cpp
    HighlightThemeViewModel.h
    HighlightThemeViewModel.cpp
    FileDecorationRulesStore.h
    FileDecorationRulesStore.cpp
    FileDecorationRulesSaveWorker.h
    FileDecorationRulesSaveWorker.cpp
    FileDecorationsViewModel.h
    FileDecorationsViewModel.cpp
)

target_include_directories(explorer_adapters_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Gui is needed for QColor (HighlightThemeViewModel's public API), on top of Qt6::Core for
# QByteArray/QJsonDocument.
target_link_libraries(explorer_adapters_config PUBLIC explorer_adapters_viewmodels explorer_adapters_highlighting explorer_adapters_decoration Qt6::Core Qt6::Gui)
