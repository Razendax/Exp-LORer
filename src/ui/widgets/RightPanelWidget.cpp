#include "RightPanelWidget.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiColors.h"

RightPanelWidget::RightPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerStrip = new QWidget(this);
    m_headerStrip->setAttribute(Qt::WA_StyledBackground, true);
    m_headerStrip->setStyleSheet(QString("background-color: %1;").arg(QLatin1String(UiColors::kRightPanelBackground)));
    auto* headerLayout = new QVBoxLayout(m_headerStrip);
    headerLayout->setContentsMargins(2, 4, 2, 4);
    headerLayout->setSpacing(2);
    headerLayout->addStretch(1); // buttons are inserted before this, keeping them top-aligned

    m_stack = new QStackedWidget(this);
    m_stack->hide();

    layout->addWidget(m_headerStrip);
    layout->addWidget(m_stack, 1);
}

int RightPanelWidget::addPanelTab(const QString& label, QWidget* content)
{
    const int index = m_stack->addWidget(content);

    auto* button = new QToolButton(m_headerStrip);
    button->setText(label);
    button->setCheckable(true);

    auto* headerLayout = static_cast<QVBoxLayout*>(m_headerStrip->layout());
    headerLayout->insertWidget(static_cast<int>(m_buttons.size()), button);

    connect(button, &QToolButton::clicked, this, [this, index]() { onPanelTabButtonClicked(index); });

    m_buttons.push_back(button);
    m_everActivated.push_back(false);

    return index;
}

void RightPanelWidget::onPanelTabButtonClicked(int index)
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

void RightPanelWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Dragging the owning QSplitter's handle can grow this widget past its collapsed (header-only)
    // width without ever going through onPanelTabButtonClicked -- the panel is at minimum size
    // with no tab selected (m_activeIndex reset to -1 on collapse, see onPanelTabButtonClicked) and
    // the user drags the divider out. Treat that the same as clicking the first panel tab, so the
    // user always sees content rather than blank space next to the header strip.
    if (m_activeIndex < 0 && !m_buttons.empty() && width() > collapsedWidth())
    {
        onPanelTabButtonClicked(0);
    }
}

int RightPanelWidget::collapsedWidth() const
{
    return m_headerStrip->sizeHint().width();
}

int RightPanelWidget::preferredExpandedWidth() const
{
    constexpr int kContentWidth = 260;
    return collapsedWidth() + kContentWidth;
}
