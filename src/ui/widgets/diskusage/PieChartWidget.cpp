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
    // the whole stack short enough to fit a bottom panel's height even with 11 wedges. Labels are
    // listed in wedge order (heaviest first) rather than by their wedge's angular position, and are
    // not connected back to their wedge by a leader line -- the swatch color is the only tie.
    constexpr int kLabelBoxWidth = 220;  // text width budget for "[name]  NN.N%  size"
    constexpr int kSwatchSize = 10;      // filled square echoing the wedge's color, drawn before the name
    constexpr int kSwatchGap = 6;        // swatch -> text
    constexpr int kPieToLabelGap = 24;   // horizontal gap between the pie's rim and the label column
    constexpr int kLabelVerticalPadding = 5;

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
    const int reservedRight = kPieToLabelGap + kSwatchSize + kSwatchGap + kLabelBoxWidth;
    const int availableWidth = std::max(width() - 2 * kMargin - reservedRight, 0);
    const int availableHeight = std::max(height() - 2 * kMargin, 0);
    const int side = std::min(availableWidth, availableHeight);
    QRect bounds(0, 0, side, side);
    bounds.translate(kMargin + (availableWidth - side) / 2, (height() - side) / 2);
    return bounds;
}

double PieChartWidget::labelColumnX() const
{
    return chartRect().right() + kPieToLabelGap;
}

// Shared by paintEvent (drawing) and mousePressEvent (hit-testing): one row per wedge, in wedge
// order (heaviest first, since the caller already sorted topChildren that way), filling down from
// the top of the label column at natural text height. Only when there isn't room for all of them
// at that natural spacing do rows switch to being spread evenly over the whole available band
// instead -- order is preserved either way, so the column always reads heaviest-to-lightest top
// to bottom.
std::vector<PieChartWidget::LabelLayout> PieChartWidget::computeLabelLayout() const
{
    std::vector<LabelLayout> layout;
    if (m_wedges.empty())
    {
        return layout;
    }

    const QFontMetrics fm = fontMetrics();
    const double labelHeight = fm.height();
    const double labelX = labelColumnX();
    const double rowWidth = kSwatchSize + kSwatchGap + kLabelBoxWidth;
    const std::size_t n = m_wedges.size();

    const double step = labelHeight + kLabelVerticalPadding;
    const double minCenterY = kMargin + labelHeight / 2.0;
    const double maxCenterY = height() - kMargin - labelHeight / 2.0;
    const double band = maxCenterY - minCenterY;
    const double naturalSpan = static_cast<double>(n - 1) * step;

    std::vector<double> centersY(n);
    if (n == 1)
    {
        centersY[0] = minCenterY;
    }
    else if (band <= 0.0 || naturalSpan > band)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            centersY[i] = minCenterY + std::max(band, 0.0) * static_cast<double>(i) / static_cast<double>(n - 1);
        }
    }
    else
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            centersY[i] = minCenterY + static_cast<double>(i) * step;
        }
    }

    layout.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const Wedge& wedge = m_wedges[i];
        const double percentage = m_totalSize == 0
            ? 0.0
            : static_cast<double>(wedge.node.size) * 100.0 / static_cast<double>(m_totalSize);

        // The percentage and size always stay readable; only an over-long name gives way.
        const QString suffix = QStringLiteral("  %1%  %2")
                                    .arg(percentage, 0, 'f', 1)
                                    .arg(FileSizeFormat::format(wedge.node.size));
        const QString name = QStringLiteral("[%1]").arg(QString::fromStdString(wedge.node.name));
        const int nameBudget = std::max(kLabelBoxWidth - fm.horizontalAdvance(suffix), 3 * kSwatchSize);

        LabelLayout entry;
        entry.wedgeIndex = i;
        entry.centerY = centersY[i];
        entry.clickRect = QRectF(labelX, centersY[i] - step / 2.0, rowWidth, step);
        entry.text = fm.elidedText(name, Qt::ElideMiddle, nameBudget) + suffix;
        layout.push_back(entry);
    }

    return layout;
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

    const QFontMetrics fm = painter.fontMetrics();
    const double labelHeight = fm.height();
    const double labelX = labelColumnX();

    for (const LabelLayout& entry : computeLabelLayout())
    {
        const Wedge& wedge = m_wedges[entry.wedgeIndex];
        const QColor color = wedge.isOther ? otherWedgeColor() : wedgeColor(entry.wedgeIndex);

        painter.setBrush(color);
        painter.setPen(QPen(palette().window().color(), 1));
        painter.drawRect(QRectF(labelX, entry.centerY - kSwatchSize / 2.0, kSwatchSize, kSwatchSize));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(labelX + kSwatchSize + kSwatchGap, entry.centerY - labelHeight / 2.0,
                                 kLabelBoxWidth, labelHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, entry.text);
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

    if (const Wedge* wedge = wedgeAt(event->pos()))
    {
        if (wedge->node.isDirectory)
        {
            emit segmentClicked(wedge->node.path);
        }
        return;
    }

    for (const LabelLayout& entry : computeLabelLayout())
    {
        if (entry.clickRect.contains(event->pos()))
        {
            const Wedge& wedge = m_wedges[entry.wedgeIndex];
            if (wedge.node.isDirectory)
            {
                emit segmentClicked(wedge.node.path);
            }
            break;
        }
    }
}
