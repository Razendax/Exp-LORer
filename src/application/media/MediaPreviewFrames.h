#pragma once

// Shared definition of the Media Viewer grid's video hover-scrub preview frames
// (Architecture.md §14.64): kCount equally-spaced time positions per video, used by
// FFmpegMediaDecoder (seek position), MediaThumbnailLoadingWorker (per-video decode loop), and
// the UI layer (hover-band count) -- one place so the three can't drift apart.
namespace MediaPreviewFrames
{
    inline constexpr int kCount = 5;

    // Fraction (0..1) into the stream's duration that `index` (0-based, 0 <= index < kCount)
    // should seek to -- the center of that index's equal time segment. index 0 reproduces the
    // ~10% position the Media Viewer's single poster frame already used before hover-scrub
    // previews existed.
    inline constexpr double fractionForIndex(int index)
    {
        return (static_cast<double>(index) + 0.5) / static_cast<double>(kCount);
    }
}
