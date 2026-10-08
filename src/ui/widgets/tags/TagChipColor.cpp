#include "TagChipColor.h"

#include <QColor>

QString tagChipTextColorFor(const QColor& background)
{
    const double luminance = 0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue();
    return luminance > 140 ? QStringLiteral("#000000") : QStringLiteral("#FFFFFF");
}
