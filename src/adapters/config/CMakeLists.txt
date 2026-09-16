add_library(explorer_adapters_config STATIC
    AppConfig.h
    AppConfigStore.h
    AppConfigStore.cpp
)

target_include_directories(explorer_adapters_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_config PUBLIC explorer_adapters_viewmodels Qt6::Core)
