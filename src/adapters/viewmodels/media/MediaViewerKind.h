#pragma once

// Which media type Media Viewer mode's grid is currently showing (Architecture.md §14.61/§14.80).
// Plain C++, no Qt/UI framework dependency, same minimal posture as ViewMode.h. Animation (GIF)
// is carved out of what would otherwise be the Image bucket -- see
// MediaExtensions::isAnimatedImageExtension.
enum class MediaViewerKind
{
    Image,
    Video,
    Animation,
};
