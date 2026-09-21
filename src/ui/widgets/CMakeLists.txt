add_library(explorer_ui_widgets STATIC
    MainWindow.h
    MainWindow.cpp
    FileBrowserView.h
    FileBrowserView.cpp
    FileNameEditDelegate.h
    FileNameEditDelegate.cpp
    FileTileDelegate.h
    FileTileDelegate.cpp
    FileIconDelegate.h
    FileIconDelegate.cpp
    SearchResultDelegate.h
    SearchResultDelegate.cpp
    SearchCriteriaPanel.h
    SearchCriteriaPanel.cpp
    SearchResultsPane.h
    SearchResultsPane.cpp
    WorkspacePaneWidget.h
    WorkspacePaneWidget.cpp
    AddressBarWidget.h
    AddressBarWidget.cpp
    StatusBarWidget.h
    StatusBarWidget.cpp
    WorkspaceLayoutWidget.h
    WorkspaceLayoutWidget.cpp
    FlowLayout.h
    FlowLayout.cpp
    TagChipWidget.h
    TagChipWidget.cpp
    TagPanelWidget.h
    TagPanelWidget.cpp
    TagManagerDialog.h
    TagManagerDialog.cpp
    ContextMenuBuilder.h
    ContextMenuBuilder.cpp
    TerminalWidget.h
    TerminalWidget.cpp
    BottomPanelWidget.h
    BottomPanelWidget.cpp
    RightPanelWidget.h
    RightPanelWidget.cpp
    PreviewPanelWidget.h
    PreviewPanelWidget.cpp
    SyntaxHighlighter.h
    SyntaxHighlighter.cpp
    SettingsDialog.h
    SettingsDialog.cpp
)

target_include_directories(explorer_ui_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_ui_widgets PUBLIC Qt6::Widgets explorer_adapters_viewmodels explorer_adapters_config
                                                  explorer_adapters_terminal explorer_adapters_highlighting)
