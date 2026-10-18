#include "BottomPanelWidget.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiColors.h"
#include "UiThemeChangeNotifier.h"

BottomPanelWidget::BottomPanelWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bottomPanelWidget"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerStrip = new QWidget(this);
    m_headerStrip->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QHBoxLayout(m_headerStrip);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(2);
    headerLayout->addStretch(1); // buttons are inserted before this, keeping them left-aligned

    m_stack = new QStackedWidget(this);
    m_stack->hide();

    layout->addWidget(m_headerStrip);
    layout->addWidget(m_stack, 1);

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &BottomPanelWidget::applyThemeColors);
}

void BottomPanelWidget::applyThemeColors()
{
    m_headerStrip->setStyleSheet(QString("background-color: %1;").arg(QLatin1String(UiColors::current().bottomPanelBackground)));
}

int BottomPanelWidget::addPanelTab(const QString& label, QWidget* content)
{
    const int index = m_stack->addWidget(content);

    auto* button = new QToolButton(m_headerStrip);
    // Index-derived so each panel tab's button keeps a distinct, stable id (§11's UI-Automation-id
    // convention) now that a second tab ("Folder Size", Architecture.md §14.39) has joined Terminal.
    button->setObjectName(QStringLiteral("bottomPanelTabButton_%1").arg(index));
    button->setText(label);
    button->setCheckable(true);

    auto* headerLayout = static_cast<QHBoxLayout*>(m_headerStrip->layout());
    headerLayout->insertWidget(static_cast<int>(m_buttons.size()), button);

    connect(button, &QToolButton::clicked, this, [this, index]() { onPanelTabButtonClicked(index); });

    m_buttons.push_back(button);
    m_everActivated.push_back(false);

    return index;
}

void BottomPanelWidget::onPanelTabButtonClicked(int index)
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

void BottomPanelWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Dragging the owning QSplitter's handle can grow this widget past its collapsed (header-only)
    // height without ever going through onPanelTabButtonClicked -- the panel is at minimum size
    // with no tab selected (m_activeIndex reset to -1 on collapse, see onPanelTabButtonClicked) and
    // the user drags the divider up. Treat that the same as clicking the first panel tab, so the
    // user always sees content rather than blank space below the header strip.
    //
    // Only do this when it's actually a handle drag, i.e. the splitter's own total height is
    // unchanged from the last resize (just redistributed between its children) -- NOT when the
    // whole window was resized/maximized, which proportionally redistributes space across the
    // splitter's children too and can nudge a deliberately-collapsed panel's height past
    // collapsedHeight() without anyone dragging anything. Without this guard, maximizing the window
    // after collapsing a deliberately-collapsed terminal panel silently re-expands it, and that then
    // gets persisted on close (Architecture.md §14.48) -- sticking the panel back open on every
    // future launch. Mirrors SidePanelWidget's own m_lastKnownSplitterWidth guard.
    int splitterHeight = -1;
    if (auto* splitter = qobject_cast<QSplitter*>(parentWidget()))
    {
        splitterHeight = splitter->height();
    }
    const bool likelyHandleDrag = splitterHeight >= 0 && splitterHeight == m_lastKnownSplitterHeight;

    if (m_activeIndex < 0 && !m_buttons.empty() && height() > collapsedHeight() && likelyHandleDrag)
    {
        onPanelTabButtonClicked(0);
    }

    m_lastKnownSplitterHeight = splitterHeight;
}

int BottomPanelWidget::collapsedHeight() const
{
    return m_headerStrip->sizeHint().height();
}

int BottomPanelWidget::preferredExpandedHeight() const
{
    constexpr int kContentHeight = 220;
    return collapsedHeight() + kContentHeight;
}
