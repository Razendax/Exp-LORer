#pragma once

#include <cstdint>
#include <optional>
#include <vector>

class QFont;
class QFontMetrics;
class QPainter;
class QPoint;
class QRect;
class Tag;

// Shared tag-pill-row painting (Architecture.md §14.x/§14.69) -- lifted out of
// TagsCellDelegate.cpp's anonymous namespace (the pill-paint loop, including the trailing "+N"
// overflow pill), now used by TagsCellDelegate and MediaDetailsDelegate (2 call sites, the third
// being the row-height query below). Unlike the small per-delegate layout-rect math this codebase
// deliberately keeps duplicated, this non-trivial pixel-painting loop earns a shared helper.
namespace TagPillRowPainter
{
    // Paints `tags` left-to-right starting at `rect`'s top-left, one rounded pill per tag,
    // clipping extras with a trailing "+N" pill if they don't fit `rect`'s width.
    void paintTagPillRow(QPainter* painter, const QRect& rect, const std::vector<Tag>& tags, const QFont& font);

    // Height of one pill row for `fontMetrics` -- lets a caller size a layout rect before calling
    // paintTagPillRow() above.
    int tagPillRowHeight(const QFontMetrics& fontMetrics);

    // Returns the id of the tag whose pill contains `pos` within `rect` (laid out exactly as
    // paintTagPillRow would), or nullopt if pos misses every pill, including the "+N" overflow
    // pill, which isn't associated with any single tag (Architecture.md §14.72).
    std::optional<std::int64_t> tagIdAt(const QRect& rect, const std::vector<Tag>& tags, const QFont& font, const QPoint& pos);
}
