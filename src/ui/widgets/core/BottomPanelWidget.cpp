#include "BottomPanelWidget.h"

#include <QStackedWidget>
#include <QVBoxLayout>

#include "UiColors.h"

BottomPanelWidget::BottomPanelWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : CollapsiblePanelWidget(Axis::Vertical, Qt::Horizontal, [] { return QString(UiColors::current().bottomPanelBackground); },
                             uiThemeChangeNotifier, parent)
{
    setObjectName(QStringLiteral("bottomPanelWidget"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_headerStrip);
    layout->addWidget(m_stack, 1);
}

int BottomPanelWidget::addPanelTab(const QString& label, QWidget* content)
{
    // Index-derived so each panel tab's button keeps a distinct, stable id (§11's UI-Automation-id
    // convention) now that a second tab ("Folder Size", Architecture.md §14.39) has joined Terminal.
    const QString buttonObjectName = QStringLiteral("bottomPanelTabButton_%1").arg(static_cast<int>(panelTabCount()));
    return addPanelTabCore(label, content, buttonObjectName);
}

int BottomPanelWidget::preferredExpandedHeight() const
{
    constexpr int kContentHeight = 220;
    return collapsedHeight() + kContentHeight;
}
