#pragma once

#include "ViewMode.h"

// Which of SHGetFileInfoW's two icon sizes (SHGFI_SMALLICON/SHGFI_LARGEICON) a view mode actually
// needs. Resolving (and caching) both unconditionally, as IconLoadingWorker used to, wastes half
// its shell/GDI calls whenever the active view only ever asks for one of them.
enum class IconSizeMask
{
    Small = 1,
    Large = 2,
    Both = Small | Large,
};

inline bool hasFlag(IconSizeMask mask, IconSizeMask flag)
{
    return (static_cast<int>(mask) & static_cast<int>(flag)) != 0;
}

inline IconSizeMask combine(IconSizeMask lhs, IconSizeMask rhs)
{
    return static_cast<IconSizeMask>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

// Mirrors FileBrowserView.cpp's iconSizeFor(ViewMode): SmallIcons/List/Details paint the 16x16
// small icon; every icon-grid mode (including Extra Large, which stretches it) paints the large
// one.
inline IconSizeMask iconSizeMaskFor(ViewMode mode)
{
    switch (mode)
    {
        case ViewMode::SmallIcons:
        case ViewMode::List:
        case ViewMode::Details:
            return IconSizeMask::Small;
        case ViewMode::ExtraLargeIcons:
        case ViewMode::LargeIcons:
        case ViewMode::MediumIcons:
        case ViewMode::Tiles:
            return IconSizeMask::Large;
    }
    return IconSizeMask::Large;
}
