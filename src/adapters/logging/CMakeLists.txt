add_library(explorer_adapters_logging STATIC
    Logging.h
    Logging.cpp
)

target_include_directories(explorer_adapters_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_logging PUBLIC spdlog::spdlog)
