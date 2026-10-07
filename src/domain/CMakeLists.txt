add_library(explorer_domain STATIC
    Error.h
    Result.h
    PathUtf8.h
    PathHash.h
    WindowsLongPath.h
    PathContainment.h
    PathContainment.cpp
    UnicodeCaseFold.h
    UnicodeCaseFold.cpp
    GlobMatch.h
    GlobMatch.cpp
    WholeWordMatch.h
    WholeWordMatch.cpp
    RegexMatch.h
    RegexMatch.cpp
    FileNode.h
    FileNode.cpp
    Tag.h
    Tag.cpp
    MediaMetadata.h
    MediaMetadata.cpp
    FileTagAssociation.h
    FileTagAssociation.cpp
    FilePreview.h
    FilePreview.cpp
    DiskUsageNode.h
    BulkRenamePattern.h
    BulkRenamePattern.cpp
)

target_include_directories(explorer_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
