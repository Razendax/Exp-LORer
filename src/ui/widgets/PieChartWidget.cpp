#include "PieChartWidget.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>

#include "FileSizeFormat.h"
#include "TagColorPalette.h"

namespace
{
    constexpr int kMargin = 8;
    constexpr double kPi = 3.14159265358979323846;

    // Legend-style label column (Architecture.md §14.40 UI polish): every label sits in one column
    // on the *right* of the pie -- "<swatch> [name]  NN.N%  size" -- so the pie is shrunk only on
    // that side. Each label is a single line (the name elides if the row is too narrow), which keeps
    // the whole stack short enough to fit a bottom panel's height even with 11 wedges.
    constexpr int kLabelBoxWidth = 170;  // text width budget for "[name]  NN.N%  size"
    constexpr int kSwatchSize = 10;      // filled square echoing the wedge's color, drawn before the name
    constexpr int kSwatchGap = 6;        // swatch -> text
    constexpr int kTrunkLength = 18;     // horizontal run from the pie's rim out to the fan-out column
    // Wide enough that the branches leaving a shared trunk read as a fan rather than collapsing
    // into one near-vertical bundle: a group's branches get kLeaderGap - kLeaderRunIn of horizontal
    // travel to spread over whatever vertical distance separates their labels.
    constexpr int kLeaderGap = 36;       // fan-out column -> swatch
    constexpr int kLeaderRunIn = 6;      // short horizontal run into the label, so the leader meets it level
    constexpr int kLabelVerticalPadding = 5;

    // A wedge thinner than this shares one merged leader trunk with its (equally thin) angular
    // neighbours instead of drawing its own near-identical line out of the circle: the group leaves
    // the rim as a single horizontal line and only fans out into per-label branches once it is clear
    // of the pie. 2% of the circle is ~7 degrees -- below that, individual leaders visually collide.
    constexpr double kTinyWedgeFraction = 0.02;

    QColor wedgeColor(std::size_t index)
    {
        return QColor(QLatin1String(TagColorPalette::kColors[index % TagColorPalette::kColors.size()]));
    }

    // Fixed neutral color for the synthetic "Other" wedge -- deliberately not wedgeColor(index), since
    // that would collide (mod the palette size) with a real wedge's color once 10 children are shown.
    QColor otherWedgeColor()
    {
        return QColor(QStringLiteral("#9E9E9E"));
    }

    // Leader lines are deliberately *not* tinted per wedge -- the swatch in front of each name
    // already carries the color, so coloring the lines too only adds noise.
    QColor leaderColor(const QPalette& widgetPalette)
    {
        QColor color = widgetPalette.text().color();
        color.setAlpha(110);
        return color;
    }
}

PieChartWidget::PieChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pieChartWidget"));
}

void PieChartWidget::setData(const std::filesystem::path& folder, const std::vector<DiskUsageNode>& topChildren,
                              std::uintmax_t totalSize)
{
    m_folder = folder;
    m_totalSize = totalSize;
    m_wedges.clear();

    double cumulative = 0.0;
    std::uintmax_t shownSize = 0;
    for (const DiskUsageNode& node : topChildren)
    {
        if (m_totalSize == 0)
        {
            break;
        }

        const double fraction = static_cast<double>(node.size) / static_cast<double>(m_totalSize);

        Wedge wedge;
        wedge.node = node;
        wedge.startFraction = cumulative;
        wedge.fraction = fraction;
        m_wedges.push_back(wedge);

        cumulative += fraction;
        shownSize += node.size;
    }

    if (m_totalSize > shownSize)
    {
        const std::uintmax_t otherSize = m_totalSize - shownSize;

        DiskUsageNode otherNode;
        otherNode.name = "Other";
        otherNode.size = otherSize;
        otherNode.isDirectory = false; // not a real path -- keeps it non-clickable via the existing isDirectory check

        Wedge wedge;
        wedge.node = otherNode;
        wedge.startFraction = cumulative;
        wedge.fraction = static_cast<double>(otherSize) / static_cast<double>(m_totalSize);
        wedge.isOther = true;
        m_wedges.push_back(wedge);
    }

    update();
}

QRect PieChartWidget::chartRect() const
{
    // The label column lives entirely on the right, so only that side is reserved; the circle is
    // then centered in whatever space is left on the left.
    const int reservedRight = kTrunkLength + kLeaderGap + kSwatchSize + kSwatchGap + kLabelBoxWidth;
    const int availableWidth = std::max(width() - 2 * kMargin - reservedRight, 0);
    const int availableHeight = std::max(height() - 2 * kMargin, 0);
    const int side = std::min(availableWidth, availableHeight);
    QRect bounds(0, 0, side, side);
    bounds.translate(kMargin + (availableWidth - side) / 2, (height() - side) / 2);
    return bounds;
}

void PieChartWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_wedges.empty())
    {
        painter.setPen(palette().text().color());
        painter.drawText(rect(), Qt::AlignCenter, tr("No data - scan this folder"));
        return;
    }

    const QRect pieRect = chartRect();

    // QPainter::drawPie angles are in 1/16ths of a degree, measured counterclockwise from 3
    // o'clock. Each wedge starts at 12 o'clock (90 degrees) minus its cumulative clockwise sweep
    // so far, and its span is negated to sweep clockwise (matching reading order: largest wedge
    // first, going clockwise from the top).
    for (std::size_t i = 0; i < m_wedges.size(); ++i)
    {
        const Wedge& wedge = m_wedges[i];
        const double startAngleDegrees = 90.0 - wedge.startFraction * 360.0;
        const double spanAngleDegrees = wedge.fraction * 360.0;

        painter.setBrush(wedge.isOther ? otherWedgeColor() : wedgeColor(i));
        painter.setPen(QPen(palette().window().color(), 1));
        painter.drawPie(pieRect, static_cast<int>(std::lround(startAngleDegrees * 16.0)),
                         -static_cast<int>(std::lround(spanAngleDegrees * 16.0)));
    }

    const QPointF center = pieRect.center();
    const double pieRadius = pieRect.width() / 2.0;
    const QFontMetrics fm = painter.fontMetrics();
    const double labelHeight = fm.height();

    // One label per wedge, stacked in the right-hand column. `idealY` is the height of the wedge's
    // own mid-angle point on the rim -- the stack is sorted by it and then decluttered, so reading
    // order down the column still follows the wedges around the circle.
    struct LabelPlacement
    {
        std::size_t wedgeIndex = 0;
        double idealY = 0.0;
        double y = 0.0;
        QString text;
    };

    std::vector<LabelPlacement> labels;
    labels.reserve(m_wedges.size());
    for (std::size_t i = 0; i < m_wedges.size(); ++i)
    {
        const Wedge& wedge = m_wedges[i];
        const double midAngleRadians = (90.0 - (wedge.startFraction + wedge.fraction / 2.0) * 360.0) * kPi / 180.0;
        const double percentage = m_totalSize == 0
            ? 0.0
            : static_cast<double>(wedge.node.size) * 100.0 / static_cast<double>(m_totalSize);

        // The percentage and size always stay readable; only an over-long name gives way.
        const QString suffix = QStringLiteral("  %1%  %2")
                                    .arg(percentage, 0, 'f', 1)
                                    .arg(FileSizeFormat::format(wedge.node.size));
        const QString name = QStringLiteral("[%1]").arg(QString::fromStdString(wedge.node.name));
        const int nameBudget = std::max(kLabelBoxWidth - fm.horizontalAdvance(suffix), 3 * kSwatchSize);

        LabelPlacement placement;
        placement.wedgeIndex = i;
        placement.idealY = center.y() - std::sin(midAngleRadians) * pieRadius;
        placement.y = placement.idealY;
        placement.text = fm.elidedText(name, Qt::ElideMiddle, nameBudget) + suffix;
        labels.push_back(placement);
    }

    std::sort(labels.begin(), labels.end(),
               [](const LabelPlacement& a, const LabelPlacement& b) { return a.idealY < b.idealY; });

    // Two-pass nearest-neighbour push (the usual donut-callout declutter), then a whole-stack shift
    // back inside the widget -- shifting the stack rather than clamping individual labels, which
    // would reintroduce overlap at the clamped end.
    const double step = labelHeight + kLabelVerticalPadding;
    for (std::size_t i = 1; i < labels.size(); ++i)
    {
        labels[i].y = std::max(labels[i].y, labels[i - 1].y + step);
    }
    for (std::size_t i = labels.size(); i > 1; --i)
    {
        const std::size_t idx = i - 2;
        labels[idx].y = std::min(labels[idx].y, labels[idx + 1].y - step);
    }

    const double minY = kMargin + labelHeight / 2.0;
    const double maxY = height() - kMargin - labelHeight / 2.0;
    const double band = maxY - minY;
    if (band <= 0.0 || labels.back().y - labels.front().y > band)
    {
        // Too many labels for the panel's height even at minimum spacing: spread them evenly over
        // the band instead of letting the ends run off-widget. Order is preserved, so the column
        // still reads in wedge order; the rows just crowd.
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            labels[i].y = labels.size() == 1
                ? (minY + maxY) / 2.0
                : minY + std::max(band, 0.0) * static_cast<double>(i) / static_cast<double>(labels.size() - 1);
        }
    }
    else
    {
        double shift = 0.0;
        if (labels.front().y < minY)
        {
            shift = minY - labels.front().y;
        }
        else if (labels.back().y > maxY)
        {
            shift = maxY - labels.back().y;
        }
        for (LabelPlacement& placement : labels)
        {
            placement.y += shift;
        }
    }

    std::vector<double> labelYByWedge(m_wedges.size(), 0.0);
    for (const LabelPlacement& placement : labels)
    {
        labelYByWedge[placement.wedgeIndex] = placement.y;
    }

    // Leader trunks. Angularly adjacent wedges that are all "tiny" share one trunk; every other
    // wedge forms a group of one, drawn by exactly the same code below.
    struct LeaderGroup
    {
        double exitX = 0.0;  // where the trunk leaves the pie's rim
        double trunkY = 0.0; // the trunk's (horizontal) height
        std::size_t firstWedge = 0;
        std::size_t lastWedge = 0;
    };

    std::vector<LeaderGroup> groups;
    for (std::size_t i = 0; i < m_wedges.size();)
    {
        std::size_t last = i;
        if (m_wedges[i].fraction < kTinyWedgeFraction)
        {
            while (last + 1 < m_wedges.size() && m_wedges[last + 1].fraction < kTinyWedgeFraction)
            {
                ++last;
            }
        }

        const double groupMidFraction =
            (m_wedges[i].startFraction + m_wedges[last].startFraction + m_wedges[last].fraction) / 2.0;
        const double midAngleRadians = (90.0 - groupMidFraction * 360.0) * kPi / 180.0;

        LeaderGroup group;
        group.trunkY = center.y() - std::sin(midAngleRadians) * pieRadius;
        // Mirrored onto the *right* rim for a wedge pointing left, so the trunk never crosses the
        // pie: it leaves the circle horizontally at the wedge's own height, and the label's color
        // swatch is what ties it back to the wedge.
        group.exitX = center.x() + std::abs(std::cos(midAngleRadians)) * pieRadius;
        group.firstWedge = i;
        group.lastWedge = last;
        groups.push_back(group);

        i = last + 1;
    }

    const double fanX = center.x() + pieRadius + kTrunkLength;
    const double labelX = fanX + kLeaderGap;

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(leaderColor(palette()), 1.0));
    for (const LeaderGroup& group : groups)
    {
        painter.drawLine(QPointF(group.exitX, group.trunkY), QPointF(fanX, group.trunkY));

        for (std::size_t i = group.firstWedge; i <= group.lastWedge; ++i)
        {
            const double y = labelYByWedge[i];
            painter.drawLine(QPointF(fanX, group.trunkY), QPointF(labelX - kLeaderRunIn, y));
            painter.drawLine(QPointF(labelX - kLeaderRunIn, y), QPointF(labelX, y));
        }
    }

    for (const LabelPlacement& placement : labels)
    {
        const Wedge& wedge = m_wedges[placement.wedgeIndex];
        const QColor color = wedge.isOther ? otherWedgeColor() : wedgeColor(placement.wedgeIndex);

        painter.setBrush(color);
        painter.setPen(QPen(leaderColor(palette()), 1.0));
        painter.drawRect(QRectF(labelX, placement.y - kSwatchSize / 2.0, kSwatchSize, kSwatchSize));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(labelX + kSwatchSize + kSwatchGap, placement.y - labelHeight / 2.0,
                                 kLabelBoxWidth, labelHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, placement.text);
    }
}

const PieChartWidget::Wedge* PieChartWidget::wedgeAt(const QPoint& widgetPos) const
{
    const QRect rect = chartRect();
    if (!rect.contains(widgetPos))
    {
        return nullptr;
    }

    const QPointF center = rect.center();
    const double dx = widgetPos.x() - center.x();
    const double dy = center.y() - widgetPos.y(); // flip: screen y grows downward, math convention grows upward

    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > rect.width() / 2.0)
    {
        return nullptr;
    }

    double angleDegrees = std::atan2(dy, dx) * 180.0 / kPi; // standard convention: CCW from 3 o'clock, in [-180, 180]
    if (angleDegrees < 0.0)
    {
        angleDegrees += 360.0;
    }

    // Convert to "clockwise sweep from 12 o'clock" fraction, matching how wedges were laid out in
    // setData/paintEvent above.
    const double clockwiseFromTopDegrees = std::fmod(450.0 - angleDegrees, 360.0);
    const double clickFraction = clockwiseFromTopDegrees / 360.0;

    for (const Wedge& wedge : m_wedges)
    {
        if (clickFraction >= wedge.startFraction && clickFraction < wedge.startFraction + wedge.fraction)
        {
            return &wedge;
        }
    }

    return nullptr;
}

void PieChartWidget::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    const Wedge* wedge = wedgeAt(event->pos());
    if (wedge && wedge->node.isDirectory)
    {
        emit segmentClicked(wedge->node.path);
    }
}
