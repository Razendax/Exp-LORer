add_library(explorer_adapters_decoration STATIC
    FileNamePattern.h
    FileNamePattern.cpp
    FileDecorationRule.h
    FileDecorationRule.cpp
    FileDecorationRules.h
    FileDecorationRules.cpp
)

target_include_directories(explorer_adapters_decoration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_decoration PUBLIC explorer_domain)
