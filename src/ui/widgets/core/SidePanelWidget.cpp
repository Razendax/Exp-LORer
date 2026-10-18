#include "SidePanelWidget.h"

#include <QHBoxLayout>
#include <QStackedWidget>

#include "UiColors.h"

SidePanelWidget::SidePanelWidget(Edge edge, const QString& idPrefix, UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : CollapsiblePanelWidget(Axis::Horizontal, Qt::Vertical, [] { return QString(UiColors::current().sidePanelBackground); },
                             uiThemeChangeNotifier, parent)
{
    setObjectName(idPrefix + QStringLiteral("Widget"));
    m_stack->setObjectName(idPrefix + QStringLiteral("Stack"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The header strip always sits on the application's outer edge, so the tab names stay put while
    // only the content stack expands toward the workspace: left edge for the left panel, right edge
    // for the right panel.
    if (edge == Edge::Right)
    {
        layout->addWidget(m_stack, 1);
        layout->addWidget(m_headerStrip);
    }
    else
    {
        layout->addWidget(m_headerStrip);
        layout->addWidget(m_stack, 1);
    }
}

int SidePanelWidget::addPanelTab(const QString& label, QWidget* content, const QString& buttonObjectName)
{
    return addPanelTabCore(label, content, buttonObjectName);
}

int SidePanelWidget::preferredExpandedWidth() const
{
    if (m_preferredExpandedWidthOverride > 0)
    {
        return m_preferredExpandedWidthOverride;
    }

    constexpr int kContentWidth = 260;
    return collapsedWidth() + kContentWidth;
}

void SidePanelWidget::setPreferredExpandedWidth(int width)
{
    if (width <= 0)
    {
        return;
    }

    m_preferredExpandedWidthOverride = width;
}
