#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QWidget>

#include "DiskUsageNode.h"

// Custom-painted pie chart (Architecture.md §14.39) -- no QtCharts dependency for one chart, same
// posture as this codebase's other hand-painted widgets (TerminalWidget, TagChipWidget). Paints up
// to 10 wedges (the caller already sorted/truncated via DiskUsageUseCase::topChildren), each sized
// by share of `totalSize`. `totalSize` is the FULL children total, not the sum of the shown
// wedges, so a folder with more than 10 children leaves a visible unfilled gap for what isn't
// individually shown -- no "Other" bucket (Architecture.md §14.39 design decision).
class PieChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PieChartWidget(QWidget* parent = nullptr);

    // Replaces the displayed data and repaints. An empty `topChildren` shows a "No data" placeholder.
    void setData(const std::filesystem::path& folder, const std::vector<DiskUsageNode>& topChildren, std::uintmax_t totalSize);

signals:
    // Fired only for a directory segment (a file isn't navigable) hit by a click.
    void segmentClicked(const std::filesystem::path& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Wedge
    {
        DiskUsageNode node;
        double startFraction = 0.0; // cumulative share of the circle swept before this wedge, in [0, 1)
        double fraction = 0.0;      // this wedge's own share of the circle, in (0, 1]
    };

    QRect chartRect() const;
    const Wedge* wedgeAt(const QPoint& widgetPos) const;

    std::filesystem::path m_folder;
    std::vector<Wedge> m_wedges;
    std::uintmax_t m_totalSize = 0;
};
