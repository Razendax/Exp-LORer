#pragma once

// Media Viewer mode's grid sort-by criterion (Architecture.md §14.68). Plain C++, no Qt/UI
// framework dependency, same minimal posture as MediaViewerKind.h.
enum class MediaViewerSortCriterion
{
    Name,
    Duration,
    Size,
};
