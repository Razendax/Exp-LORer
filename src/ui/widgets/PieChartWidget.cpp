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
    const int side = std::max(std::min(width(), height()) - 2 * kMargin, 0);
    QRect bounds(0, 0, side, side);
    bounds.translate((width() - side) / 2, (height() - side) / 2);
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

    const QRect rect = chartRect();

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
        painter.drawPie(rect, static_cast<int>(std::lround(startAngleDegrees * 16.0)),
                         -static_cast<int>(std::lround(spanAngleDegrees * 16.0)));
    }

    painter.setPen(palette().text().color());
    const QPointF center = rect.center();
    const double radius = rect.width() / 2.0;
    for (const Wedge& wedge : m_wedges)
    {
        const double midAngleDegrees = 90.0 - (wedge.startFraction + wedge.fraction / 2.0) * 360.0;
        const double midAngleRadians = midAngleDegrees * kPi / 180.0;
        const QPointF labelPoint(center.x() + std::cos(midAngleRadians) * radius * 0.65,
                                  center.y() - std::sin(midAngleRadians) * radius * 0.65);

        const double percentage = m_totalSize == 0 ? 0.0 : static_cast<double>(wedge.node.size) * 100.0 / static_cast<double>(m_totalSize);
        const QString label = QStringLiteral("%1\n%2%\n%3")
                                   .arg(QString::fromStdString(wedge.node.name))
                                   .arg(percentage, 0, 'f', 1)
                                   .arg(FileSizeFormat::format(wedge.node.size));

        painter.drawText(QRectF(labelPoint.x() - 45, labelPoint.y() - 20, 90, 40), Qt::AlignCenter | Qt::TextWordWrap, label);
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
