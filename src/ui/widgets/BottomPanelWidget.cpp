#include "BottomPanelWidget.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiColors.h"

BottomPanelWidget::BottomPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerStrip = new QWidget(this);
    m_headerStrip->setAttribute(Qt::WA_StyledBackground, true);
    m_headerStrip->setStyleSheet(QString("background-color: %1;").arg(QLatin1String(UiColors::kBottomPanelBackground)));
    auto* headerLayout = new QHBoxLayout(m_headerStrip);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(2);
    headerLayout->addStretch(1); // buttons are inserted before this, keeping them left-aligned

    m_stack = new QStackedWidget(this);
    m_stack->hide();

    layout->addWidget(m_headerStrip);
    layout->addWidget(m_stack, 1);
}

int BottomPanelWidget::addPanelTab(const QString& label, QWidget* content)
{
    const int index = m_stack->addWidget(content);

    auto* button = new QToolButton(m_headerStrip);
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
    if (m_activeIndex < 0 && !m_buttons.empty() && height() > collapsedHeight())
    {
        onPanelTabButtonClicked(0);
    }
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
