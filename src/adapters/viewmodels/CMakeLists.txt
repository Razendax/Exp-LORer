add_library(explorer_adapters_viewmodels STATIC
    NavigationHistory.h
    NavigationHistory.cpp
    NavigationViewModel.h
    NavigationViewModel.cpp
)

target_include_directories(explorer_adapters_viewmodels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_viewmodels PUBLIC explorer_application Qt6::Core)
