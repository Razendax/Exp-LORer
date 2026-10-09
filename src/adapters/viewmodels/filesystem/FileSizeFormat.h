#pragma once

#include <array>
#include <cstdint>

#include <QString>

// Shared "12.3 MB"-style human-readable byte formatting, used by both FileListModel's Size column
// and PieChartWidget's wedge labels (Architecture.md §14.39) so the two can never drift apart.
namespace FileSizeFormat
{
    inline QString format(std::uintmax_t bytes)
    {
        static constexpr std::array<const char*, 5> units = { "B", "KB", "MB", "GB", "TB" };

        double value = static_cast<double>(bytes);
        std::size_t unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < units.size())
        {
            value /= 1024.0;
            ++unitIndex;
        }

        const int precision = unitIndex == 0 ? 0 : 1;
        return QStringLiteral("%1 %2").arg(value, 0, 'f', precision).arg(QLatin1String(units[unitIndex]));
    }
}
