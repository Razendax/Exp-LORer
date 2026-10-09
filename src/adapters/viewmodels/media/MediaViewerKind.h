#pragma once

// Which media type Media Viewer mode's grid is currently showing (Architecture.md §14.61). Plain
// C++, no Qt/UI framework dependency, same minimal posture as ViewMode.h.
enum class MediaViewerKind
{
    Image,
    Video,
};
