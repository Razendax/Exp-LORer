add_library(explorer_ui_widgets STATIC
    MainWindow.h
    MainWindow.cpp
    FileBrowserView.h
    FileBrowserView.cpp
    FileTileDelegate.h
    FileTileDelegate.cpp
    FileIconDelegate.h
    FileIconDelegate.cpp
    WorkspacePaneWidget.h
    WorkspacePaneWidget.cpp
    WorkspaceLayoutWidget.h
    WorkspaceLayoutWidget.cpp
    TagChipWidget.h
    TagChipWidget.cpp
    TagPanelWidget.h
    TagPanelWidget.cpp
    ContextMenuBuilder.h
    ContextMenuBuilder.cpp
)

target_include_directories(explorer_ui_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_ui_widgets PUBLIC Qt6::Widgets explorer_adapters_viewmodels explorer_adapters_config)
