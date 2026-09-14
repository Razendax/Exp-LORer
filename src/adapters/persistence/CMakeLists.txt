add_library(explorer_adapters_persistence STATIC
    SQLiteTagRepository.h
    SQLiteTagRepository.cpp
)

target_include_directories(explorer_adapters_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_persistence PUBLIC explorer_application Qt6::Sql)
