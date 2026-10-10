#pragma once

// Media Viewer mode's grid "view" concept (Architecture.md §14.68/§14.69). Plain C++, no Qt/UI
// framework dependency, same minimal posture as MediaViewerKind.h.
enum class MediaViewerViewMode
{
    Thumbnails,
    Filmstrip,
    Details,
};
