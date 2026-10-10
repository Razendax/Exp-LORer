#pragma once

// Media Viewer mode's tag-click behaviour toggle (Architecture.md §14.72). Plain C++, no Qt/UI
// framework dependency, same minimal posture as MediaViewerKind.h/MediaViewerViewMode.h.
enum class MediaViewerTagBehaviour { Filter, Apply };
