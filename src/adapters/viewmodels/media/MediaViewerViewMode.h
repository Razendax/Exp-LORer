#pragma once

// Media Viewer mode's grid "view" concept (Architecture.md §14.68): a single value today
// (Thumbnails); future view modes are added as new enumerators + combo items, nothing else
// changes shape-wise. Plain C++, no Qt/UI framework dependency, same minimal posture as
// MediaViewerKind.h.
enum class MediaViewerViewMode
{
    Thumbnails,
};
