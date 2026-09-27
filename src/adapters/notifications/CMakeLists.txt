add_library(explorer_adapters_notifications STATIC
    NotificationType.h
    NotificationSettings.h
    NotificationCenter.h
    NotificationCenter.cpp
)

target_include_directories(explorer_adapters_notifications PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_notifications PUBLIC explorer_domain Qt6::Core)
