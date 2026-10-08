#include "TerminalSessionsWidget.h"

#include <algorithm>

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "TerminalWidget.h"
#include "UiColors.h"
#include "UiThemeChangeNotifier.h"

namespace
{
    constexpr int kSideColumnWidth = 160;
    // Just enough for the collapse-toggle button on its own.
    constexpr int kSideColumnCollapsedWidth = 28;
    constexpr const char* kSessionIdProperty = "sessionId";
    constexpr const char* kSelectedProperty = "selected";
}

TerminalSessionsWidget::TerminalSessionsWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("terminalSessionsWidget"));
    m_preferredExpandedWidth = kSideColumnWidth;

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget;
    m_emptyStatePage = new QLabel(tr("Click + to open a terminal"), m_stack);
    m_emptyStatePage->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_emptyStatePage);

    m_sideColumn = new QWidget;
    m_sideColumn->setAttribute(Qt::WA_StyledBackground, true);

    auto* columnLayout = new QVBoxLayout(m_sideColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(2);

    // Always visible, even collapsed -- the only way back to expanded once collapsed.
    m_collapseButton = new QToolButton(m_sideColumn);
    m_collapseButton->setObjectName(QStringLiteral("terminalSessionsCollapseButton"));
    m_collapseButton->setAutoRaise(true);
    m_collapseButton->setText(QStringLiteral("»"));
    m_collapseButton->setToolTip(tr("Collapse session list"));
    connect(m_collapseButton, &QToolButton::clicked, this, [this]() { setSessionsPanelExpanded(!m_sessionsPanelExpanded); });
    columnLayout->addWidget(m_collapseButton);

    // Everything else lives under one widget so collapsing the panel is a single setVisible() call
    // rather than hiding each child individually.
    m_sideContent = new QWidget(m_sideColumn);
    columnLayout->addWidget(m_sideContent, 1);

    auto* sideLayout = new QVBoxLayout(m_sideContent);
    sideLayout->setContentsMargins(4, 0, 4, 4);
    sideLayout->setSpacing(4);

    m_addSessionButton = new QToolButton(m_sideContent);
    m_addSessionButton->setObjectName(QStringLiteral("terminalAddSessionButton"));
    m_addSessionButton->setText(QStringLiteral("+"));
    m_addSessionButton->setToolTip(tr("Open a new terminal"));
    connect(m_addSessionButton, &QToolButton::clicked, this, [this]() {
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        QAction* cmdAction = menu->addAction(tr("Command Prompt"));
        connect(cmdAction, &QAction::triggered, this, [this]() { addSession(TerminalShellType::Cmd); });

        QAction* powerShellAction = menu->addAction(tr("PowerShell"));
        connect(powerShellAction, &QAction::triggered, this, [this]() { addSession(TerminalShellType::PowerShell); });

        const QPoint bottomLeft = m_addSessionButton->mapToGlobal(m_addSessionButton->rect().bottomLeft());
        menu->popup(bottomLeft);
    });

    sideLayout->addWidget(m_addSessionButton);

    m_sessionListLayout = new QVBoxLayout();
    m_sessionListLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionListLayout->setSpacing(0);
    sideLayout->addLayout(m_sessionListLayout);
    sideLayout->addStretch(1);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("terminalSessionsSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_stack);
    m_splitter->addWidget(m_sideColumn);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    // Placeholder sizes at construction time, before any real layout pass -- mirrors MainWindow's own
    // left/right-panel splitter setup, which uses the same "big number for the side that should take
    // the remaining space" trick rather than computing from width()/sizeHint() (both unreliable before
    // the widget is first shown).
    m_splitter->setSizes({ 1000, m_preferredExpandedWidth });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_sessionsPanelExpanded)
        {
            m_preferredExpandedWidth = m_sideColumn->width();
        }
    });

    layout->addWidget(m_splitter);

    applyThemeColors();
    connect(&uiThemeChangeNotifier, &UiThemeChangeNotifier::themeChanged, this, &TerminalSessionsWidget::applyThemeColors);
}

bool TerminalSessionsWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        auto* row = qobject_cast<QWidget*>(watched);
        const QVariant idProperty = row != nullptr ? row->property(kSessionIdProperty) : QVariant();
        if (idProperty.isValid())
        {
            setActiveSession(idProperty.toInt());
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TerminalSessionsWidget::applyThemeColors()
{
    const UiColors::UiColorSet& colors = UiColors::current();

    m_sideColumn->setStyleSheet(QString("background-color: %1;").arg(QLatin1String(colors.sidePanelBackground)));

    m_addSessionButton->setStyleSheet(QString("QToolButton { background-color: %1; border: none; border-radius: 3px; padding: 2px 6px; }"
                                                "QToolButton:hover { background-color: %2; }")
                                           .arg(QLatin1String(colors.advancedSearchButtonBackground),
                                                QLatin1String(colors.advancedSearchButtonHoverBackground)));

    m_collapseButton->setStyleSheet(QString("QToolButton { background-color: transparent; border: none; padding: 2px; }"
                                              "QToolButton:hover { background-color: %1; }")
                                         .arg(QLatin1String(colors.advancedSearchButtonHoverBackground)));

    for (const Session& session : m_sessions)
    {
        // Flush, square-cornered rows (no padding/border-radius "pill" look) so each one reads as a
        // list item: transparent at rest, a soft tint on hover, and a solid full-width bar -- plus
        // bold label text -- while selected. Hover works because the label is marked
        // WA_TransparentForMouseEvents, so the row itself (not the label) receives the mouse-move/
        // :hover state.
        session.row->setStyleSheet(QString("QWidget#%1 { background-color: transparent; }"
                                             "QWidget#%1:hover { background-color: %2; }"
                                             "QWidget#%1[selected=\"true\"] { background-color: %3; }"
                                             "QWidget#%1[selected=\"true\"] QLabel { font-weight: 600; }")
                                        .arg(session.row->objectName(), QLatin1String(colors.advancedSearchButtonHoverBackground),
                                             QLatin1String(colors.activeTabBackground)));

        // Flat/transparent until hovered, so the row reads as one list item with a close affordance
        // rather than two adjacent controls -- only turns into the destructive-red button (same
        // convention as SearchCriteriaPanel's close button) when the user is actually about to click
        // it.
        session.closeButton->setStyleSheet(
            QString("QToolButton { color: palette(text); background: transparent; border: none; border-radius: 4px; }"
                    "QToolButton:hover { color: white; background-color: %1; }"
                    "QToolButton:pressed { background-color: %2; }")
                .arg(QLatin1String(colors.closeButtonRed), QLatin1String(colors.closeButtonRedHover)));
    }
}

QString TerminalSessionsWidget::nextSessionLabel(TerminalShellType shellType)
{
    int& counter = shellType == TerminalShellType::PowerShell ? m_powerShellSessionCount : m_cmdSessionCount;
    ++counter;
    return QStringLiteral("%1 %2").arg(terminalShellDisplayName(shellType)).arg(counter);
}

void TerminalSessionsWidget::activateDefaultSession()
{
    if (m_sessions.empty())
    {
        addSession(TerminalShellType::Cmd);
    }
}

void TerminalSessionsWidget::addSession(TerminalShellType shellType)
{
    const int id = m_nextSessionId++;
    const QString label = nextSessionLabel(shellType);

    auto* terminal = new TerminalWidget(shellType, m_stack);
    terminal->setObjectName(QStringLiteral("terminalWidget_%1").arg(id));
    m_stack->addWidget(terminal);

    auto* row = new QWidget(m_sideContent);
    row->setObjectName(QStringLiteral("terminalSessionRow_%1").arg(id));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setProperty(kSessionIdProperty, id);
    row->installEventFilter(this);

    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 4, 4, 4);
    rowLayout->setSpacing(4);

    auto* nameLabel = new QLabel(label, row);
    nameLabel->setObjectName(QStringLiteral("terminalSessionLabel_%1").arg(id));
    // Lets row clicks (and hover) pass through the label to the row itself -- see eventFilter().
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    rowLayout->addWidget(nameLabel, 1);

    auto* closeButton = new QToolButton(row);
    closeButton->setObjectName(QStringLiteral("terminalSessionCloseButton_%1").arg(id));
    closeButton->setText(QStringLiteral("×"));
    closeButton->setToolTip(tr("Close terminal session"));
    closeButton->setFixedSize(18, 18);
    rowLayout->addWidget(closeButton);

    m_sessionListLayout->addWidget(row);

    m_sessions.push_back(Session{ id, shellType, terminal, row, closeButton });

    connect(closeButton, &QToolButton::clicked, this, [this, id]() { closeSession(id); });

    applyThemeColors();
    setActiveSession(id);

    emit sessionCreated(terminal);
}

void TerminalSessionsWidget::setActiveSession(int id)
{
    m_activeSessionId = id;

    for (const Session& session : m_sessions)
    {
        const bool selected = session.id == id;
        applyRowSelectedStyle(session, selected);
        if (selected)
        {
            m_stack->setCurrentWidget(session.terminal);
            session.terminal->setFocus();
        }
    }
}

void TerminalSessionsWidget::applyRowSelectedStyle(const Session& session, bool selected)
{
    session.row->setProperty(kSelectedProperty, selected);
    session.row->style()->unpolish(session.row);
    session.row->style()->polish(session.row);
    session.row->update();
}

void TerminalSessionsWidget::setSessionsPanelExpanded(bool expanded)
{
    if (m_sessionsPanelExpanded == expanded)
    {
        return;
    }
    m_sessionsPanelExpanded = expanded;

    m_sideContent->setVisible(expanded);

    const int sideWidth = expanded ? m_preferredExpandedWidth : kSideColumnCollapsedWidth;
    const int total = std::max(m_splitter->width(), m_splitter->sizeHint().width());
    m_splitter->setSizes({ std::max(0, total - sideWidth), sideWidth });

    m_collapseButton->setText(expanded ? QStringLiteral("»") : QStringLiteral("«"));
    m_collapseButton->setToolTip(expanded ? tr("Collapse session list") : tr("Expand session list"));
}

void TerminalSessionsWidget::closeSession(int id)
{
    const auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [id](const Session& session) { return session.id == id; });
    if (it == m_sessions.end())
    {
        return;
    }

    m_stack->removeWidget(it->terminal);
    it->terminal->deleteLater();
    m_sessionListLayout->removeWidget(it->row);
    it->row->deleteLater();

    const bool wasActive = m_activeSessionId == id;
    m_sessions.erase(it);

    if (!wasActive)
    {
        return;
    }

    if (m_sessions.empty())
    {
        m_activeSessionId = -1;
        m_stack->setCurrentWidget(m_emptyStatePage);
    }
    else
    {
        setActiveSession(m_sessions.back().id);
    }
}
