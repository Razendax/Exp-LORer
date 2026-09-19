add_library(explorer_domain STATIC
    Error.h
    Result.h
    PathUtf8.h
    FileNode.h
    FileNode.cpp
    Tag.h
    Tag.cpp
    MediaMetadata.h
    MediaMetadata.cpp
    FileTagAssociation.h
    FileTagAssociation.cpp
)

target_include_directories(explorer_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
