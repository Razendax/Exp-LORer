#pragma once

// Media Viewer mode's per-tile GIF animation preview concept (Architecture.md §14.83). Plain C++,
// no Qt/UI framework dependency, same minimal posture as MediaViewerGroupMode.h.
enum class MediaViewerPreviewMode
{
    Static,
    AnimateOnHover,
    Autoplay,
};
