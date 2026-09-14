#pragma once

// Presentation-only state for how directory contents are displayed (Architecture.md §2.3.1).
// Not a Domain/Application concern; plain C++, no Qt/UI framework dependency.
enum class ViewMode
{
    ExtraLargeIcons,
    LargeIcons,
    MediumIcons,
    SmallIcons,
    List,
    Details,
    Tiles,
};
