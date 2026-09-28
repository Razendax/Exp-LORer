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
    MainStatusBarWidget.h
    MainStatusBarWidget.cpp
    WorkspaceLayoutWidget.h
    WorkspaceLayoutWidget.cpp
    FlowLayout.h
    FlowLayout.cpp
    TagChipColor.h
    TagChipColor.cpp
    TagChipWidget.h
    TagChipWidget.cpp
    TagsCellDelegate.h
    TagsCellDelegate.cpp
    PercentCellDelegate.h
    PercentCellDelegate.cpp
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
    MonitoringLogModel.h
    MonitoringLogModel.cpp
    MonitoringPanelWidget.h
    MonitoringPanelWidget.cpp
    PieChartWidget.h
    PieChartWidget.cpp
    FolderSizeWidget.h
    FolderSizeWidget.cpp
    PreviewPanelWidget.h
    PreviewPanelWidget.cpp
    FolderPreviewListModel.h
    FolderPreviewListModel.cpp
    SyntaxHighlighter.h
    SyntaxHighlighter.cpp
    SettingsDialog.h
    SettingsDialog.cpp
    FileDecorationRuleDialog.h
    FileDecorationRuleDialog.cpp
    ManageFavoritesDialog.h
    ManageFavoritesDialog.cpp
    ToastNotificationWidget.h
    ToastNotificationWidget.cpp
    ProgressNotificationWidget.h
    ProgressNotificationWidget.cpp
    NotificationOverlayWidget.h
    NotificationOverlayWidget.cpp
    UiColors.h
)

target_include_directories(explorer_ui_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# explorer_adapters_config already links explorer_adapters_notifications transitively (for
# NotificationSettingsViewModel's NotificationSettings&), which is what brings in NotificationType/
# NotificationCenter for SettingsDialog/MainWindow/NotificationOverlayWidget here.
target_link_libraries(explorer_ui_widgets PUBLIC Qt6::Widgets explorer_adapters_viewmodels explorer_adapters_config
                                                  explorer_adapters_terminal explorer_adapters_highlighting)
