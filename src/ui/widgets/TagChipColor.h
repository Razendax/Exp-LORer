#pragma once

#include <QString>

class QColor;

// Shared color math for tag chip/pill rendering (Architecture.md §14.9's TagChipWidget and the
// Tags column's TagsCellDelegate) -- picks readable text (black/white) against a tag's background
// color for contrast. One free function so both call sites agree, rather than duplicating the
// luminance formula.
QString tagChipTextColorFor(const QColor& background);
