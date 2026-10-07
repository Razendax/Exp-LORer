add_library(explorer_adapters_config STATIC
    AppConfig.h
    JsonFileStore.h
    JsonFileStore.cpp
    AppConfigStore.h
    AppConfigStore.cpp
    AsyncSaveWorker.h
    AsyncSaveDispatcher.h
    HighlightTheme.h
    HighlightTheme.cpp
    HighlightThemeStore.h
    HighlightThemeStore.cpp
    HighlightThemeViewModel.h
    HighlightThemeViewModel.cpp
    FileDecorationRulesStore.h
    FileDecorationRulesStore.cpp
    FileDecorationsViewModel.h
    FileDecorationsViewModel.cpp
    FavoritesStore.h
    FavoritesStore.cpp
    FavoritesViewModel.h
    FavoritesViewModel.cpp
    NotificationSettingsStore.h
    NotificationSettingsStore.cpp
    NotificationSettingsViewModel.h
    NotificationSettingsViewModel.cpp
    ColumnVisibilitySettings.h
    ColumnVisibilitySettingsStore.h
    ColumnVisibilitySettingsStore.cpp
    ColumnVisibilitySettingsViewModel.h
    ColumnVisibilitySettingsViewModel.cpp
    EnvironmentVariable.h
    EnvironmentVariablesStore.h
    EnvironmentVariablesStore.cpp
    EnvironmentVariableApplier.h
    EnvironmentVariableApplier.cpp
    EnvironmentVariablesViewModel.h
    EnvironmentVariablesViewModel.cpp
)

target_include_directories(explorer_adapters_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Gui is needed for QColor (HighlightThemeViewModel's public API), on top of Qt6::Core for
# QByteArray/QJsonDocument.
target_link_libraries(explorer_adapters_config PUBLIC explorer_adapters_viewmodels explorer_adapters_highlighting explorer_adapters_decoration explorer_adapters_favorites explorer_adapters_notifications Qt6::Core Qt6::Gui)
target_link_libraries(explorer_adapters_config PRIVATE explorer_adapters_logging)
