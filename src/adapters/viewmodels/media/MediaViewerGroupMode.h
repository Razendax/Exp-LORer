#pragma once

// Media Viewer mode's grid grouping concept (Architecture.md §14.74). Plain C++, no Qt/UI
// framework dependency, same minimal posture as MediaViewerKind.h.
enum class MediaViewerGroupMode
{
    Recursive,
    ByFolder,
};
