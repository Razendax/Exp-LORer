#include "CollapsiblePanelWidget.h"

#include <utility>

#include <QBoxLayout>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>

#include "UiThemeChangeNotifier.h"

CollapsiblePanelWidget::CollapsiblePanelWidget(Axis axis, Qt::Orientation headerButtonOrientation,
                                                std::function<QString()> headerBackgroundColor,
                                                UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
    , m_axis(axis)
    , m_headerBackgroundColor(std::move(headerBackgroundColor))
{
    m_headerStrip = new QWidget(this);
    m_headerStrip->setAttribute(Qt::WA_StyledBackground, true);

    QBoxLayout* headerLayout = nullptr;
    if (headerButtonOrientation == Qt::Vertical)
    {
        headerLayout = new QVBoxLayout(m_headerStrip);
        headerLayout->setContentsMargins(2, 4, 2, 4);
    }
    else
    {
        headerLayout = new QHBoxLayout(m_headerStrip);
        headerLayout->setContentsMargins(4, 2, 4, 2);
    }
    headerLayout->setSpacing(2);
    headerLayout->addStretch(1); // buttons are inserted before this, keeping them top-/left-aligned

    m_stack = new QStackedWidget(this);
    m_stack->hide();

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &CollapsiblePanelWidget::applyThemeColors);
}

void CollapsiblePanelWidget::applyThemeColors()
{
    m_headerStrip->setStyleSheet(QString("background-color: %1;").arg(m_headerBackgroundColor()));
}

int CollapsiblePanelWidget::addPanelTabCore(const QString& label, QWidget* content, const QString& buttonObjectName)
{
    const int index = m_stack->addWidget(content);

    auto* button = new QToolButton(m_headerStrip);
    button->setObjectName(buttonObjectName);
    button->setText(label);
    button->setCheckable(true);

    auto* headerLayout = static_cast<QBoxLayout*>(m_headerStrip->layout());
    headerLayout->insertWidget(static_cast<int>(m_buttons.size()), button);

    connect(button, &QToolButton::clicked, this, [this, index]() { onPanelTabButtonClicked(index); });

    m_buttons.push_back(button);
    m_everActivated.push_back(false);

    return index;
}

void CollapsiblePanelWidget::activateTab(int index)
{
    onPanelTabButtonClicked(index);
}

void CollapsiblePanelWidget::toggleExpanded()
{
    if (m_expanded && m_activeIndex >= 0)
    {
        // Same effect as clicking the already-active tab again: collapse.
        onPanelTabButtonClicked(m_activeIndex);
        return;
    }

    if (m_buttons.empty())
    {
        // No tabs registered at all -- nothing to expand onto.
        return;
    }

    const int target = m_lastActiveIndex >= 0 ? m_lastActiveIndex : 0;
    activateTab(target);
}

void CollapsiblePanelWidget::setPanelTabButtonVisible(int index, bool visible)
{
    m_buttons[static_cast<std::size_t>(index)]->setVisible(visible);
}

void CollapsiblePanelWidget::onPanelTabButtonClicked(int index)
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
    m_lastActiveIndex = index;
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

void CollapsiblePanelWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Dragging the owning QSplitter's handle can grow this widget past its collapsed (header-only)
    // extent without ever going through onPanelTabButtonClicked -- the panel is at minimum size
    // with no tab selected (m_activeIndex reset to -1 on collapse, see onPanelTabButtonClicked) and
    // the user drags the divider out. Treat that the same as clicking the first panel tab, so the
    // user always sees content rather than blank space next to/below the header strip.
    //
    // Only do this when it's actually a handle drag, i.e. the splitter's own total extent is
    // unchanged from the last resize (just redistributed between its children) -- NOT when the
    // whole window was resized/maximized, which proportionally redistributes space across the
    // splitter's children too and can nudge a deliberately-collapsed panel's extent past
    // collapsedExtent() without anyone dragging anything. Without this guard, maximizing the window
    // after collapsing a panel silently re-expands it, and that then gets persisted on close
    // (Architecture.md §14.48) -- sticking it back open on every future launch.
    int splitterExtent = -1;
    if (auto* splitter = qobject_cast<QSplitter*>(parentWidget()))
    {
        splitterExtent = m_axis == Axis::Horizontal ? splitter->width() : splitter->height();
    }
    const bool likelyHandleDrag = splitterExtent >= 0 && splitterExtent == m_lastKnownSplitterExtent;

    const int ownExtent = m_axis == Axis::Horizontal ? width() : height();
    if (m_activeIndex < 0 && !m_buttons.empty() && ownExtent > collapsedExtent() && likelyHandleDrag)
    {
        onPanelTabButtonClicked(0);
    }

    m_lastKnownSplitterExtent = splitterExtent;
}

int CollapsiblePanelWidget::collapsedExtent() const
{
    const QSize hint = m_headerStrip->sizeHint();
    return m_axis == Axis::Horizontal ? hint.width() : hint.height();
}
