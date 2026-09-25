add_library(explorer_adapters_favorites STATIC
    FavoriteItem.h
    FavoriteCategory.h
    Favorites.h
    Favorites.cpp
)

target_include_directories(explorer_adapters_favorites PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(explorer_adapters_favorites PUBLIC explorer_domain)
