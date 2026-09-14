add_library(explorer_adapters_viewmodels STATIC
    NavigationHistory.h
    NavigationHistory.cpp
    TabViewModel.h
    TabViewModel.cpp
    ViewMode.h
    FileListModel.h
    FileListModel.cpp
    SplitLayout.h
    WorkspacePaneId.h
    WorkspaceLayoutTopology.h
    WorkspaceLayoutTopology.cpp
    WorkspacePaneViewModel.h
    WorkspacePaneViewModel.cpp
    WorkspaceController.h
    WorkspaceController.cpp
)

target_include_directories(explorer_adapters_viewmodels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_viewmodels PUBLIC explorer_application Qt6::Core Qt6::Widgets)
