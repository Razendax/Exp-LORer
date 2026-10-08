#include "SidePanelWidget.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiColors.h"
#include "UiThemeChangeNotifier.h"

SidePanelWidget::SidePanelWidget(Edge edge, const QString& idPrefix, UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(idPrefix + QStringLiteral("Widget"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerStrip = new QWidget(this);
    m_headerStrip->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QVBoxLayout(m_headerStrip);
    headerLayout->setContentsMargins(2, 4, 2, 4);
    headerLayout->setSpacing(2);
    headerLayout->addStretch(1); // buttons are inserted before this, keeping them top-aligned

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(idPrefix + QStringLiteral("Stack"));
    m_stack->hide();

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

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &SidePanelWidget::applyThemeColors);
}

void SidePanelWidget::applyThemeColors()
{
    m_headerStrip->setStyleSheet(QString("background-color: %1;").arg(QLatin1String(UiColors::current().sidePanelBackground)));
}

int SidePanelWidget::addPanelTab(const QString& label, QWidget* content, const QString& buttonObjectName)
{
    const int index = m_stack->addWidget(content);

    auto* button = new QToolButton(m_headerStrip);
    button->setObjectName(buttonObjectName);
    button->setText(label);
    button->setCheckable(true);

    auto* headerLayout = static_cast<QVBoxLayout*>(m_headerStrip->layout());
    headerLayout->insertWidget(static_cast<int>(m_buttons.size()), button);

    connect(button, &QToolButton::clicked, this, [this, index]() { onPanelTabButtonClicked(index); });

    m_buttons.push_back(button);
    m_everActivated.push_back(false);

    return index;
}

void SidePanelWidget::activateTab(int index)
{
    onPanelTabButtonClicked(index);
}

void SidePanelWidget::onPanelTabButtonClicked(int index)
{
    if (m_expanded && m_activeIndex == index)
    {
        // Clicking the already-active tab collapses the panel. Reset m_activeIndex so a later
        // re-expand (whether by clicking a tab button again or dragging the splitter handle, see
        // resizeEvent) starts from "no tab selected" rather than silently resuming this one.
        m_expanded = false;
        m_stack->hide();
        m_buttons[static_cast<std::size_t>(index)]->setChecked(false);
        m_activeIndex = -1;
        emit expansionChanged(false);
        return;
    }

    if (m_activeIndex >= 0 && m_activeIndex != index)
    {
        m_buttons[static_cast<std::size_t>(m_activeIndex)]->setChecked(false);
    }

    m_activeIndex = index;
    m_buttons[static_cast<std::size_t>(index)]->setChecked(true);
    m_stack->setCurrentIndex(index);

    const bool wasExpanded = m_expanded;
    m_expanded = true;
    m_stack->show();

    if (!wasExpanded)
    {
        emit expansionChanged(true);
    }

    if (!m_everActivated[static_cast<std::size_t>(index)])
    {
        m_everActivated[static_cast<std::size_t>(index)] = true;
        emit panelTabFirstActivated(index);
    }
}

void SidePanelWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Dragging the owning QSplitter's handle can grow this widget past its collapsed (header-only)
    // width without ever going through onPanelTabButtonClicked -- the panel is at minimum size
    // with no tab selected (m_activeIndex reset to -1 on collapse, see onPanelTabButtonClicked) and
    // the user drags the divider out. Treat that the same as clicking the first panel tab, so the
    // user always sees content rather than blank space next to the header strip.
    //
    // Only do this when it's actually a handle drag, i.e. the splitter's own total width is
    // unchanged from the last resize (just redistributed between its children) -- NOT when the
    // whole window was resized/maximized, which proportionally redistributes space across the
    // splitter's children too and can nudge a deliberately-collapsed panel's width past
    // collapsedWidth() without anyone dragging anything. Without this guard, maximizing the window
    // after collapsing both side panels silently re-expands them, and that then gets persisted on
    // close (Architecture.md §14.48) -- sticking the panels back open on every future launch.
    int splitterWidth = -1;
    if (auto* splitter = qobject_cast<QSplitter*>(parentWidget()))
    {
        splitterWidth = splitter->width();
    }
    const bool likelyHandleDrag = splitterWidth >= 0 && splitterWidth == m_lastKnownSplitterWidth;

    if (m_activeIndex < 0 && !m_buttons.empty() && width() > collapsedWidth() && likelyHandleDrag)
    {
        onPanelTabButtonClicked(0);
    }

    m_lastKnownSplitterWidth = splitterWidth;
}

int SidePanelWidget::collapsedWidth() const
{
    return m_headerStrip->sizeHint().width();
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
