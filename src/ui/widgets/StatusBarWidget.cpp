#include "StatusBarWidget.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>

StatusBarWidget::StatusBarWidget(QWidget* parent)
    : QWidget(parent)
{
    m_countsLabel = new QLabel(this);
    m_countsLabel->setObjectName(QStringLiteral("paneStatusCountsLabel"));

    m_gitGroup = new QWidget(this);
    auto* gitLayout = new QHBoxLayout(m_gitGroup);
    gitLayout->setContentsMargins(0, 0, 0, 0);
    gitLayout->setSpacing(4);

    m_branchButton = new QToolButton(m_gitGroup);
    m_branchButton->setObjectName(QStringLiteral("paneStatusBranchButton"));
    m_branchButton->setAutoRaise(true);
    m_branchButton->hide();
    gitLayout->addWidget(m_branchButton);

    m_changedCountLabel = new QLabel(m_gitGroup);
    m_changedCountLabel->setObjectName(QStringLiteral("paneStatusChangedCountLabel"));
    m_changedCountLabel->hide();
    gitLayout->addWidget(m_changedCountLabel);

    m_quickSelectEdit = new QLineEdit(this);
    m_quickSelectEdit->setObjectName(QStringLiteral("paneStatusQuickSelectEdit"));
    m_quickSelectEdit->setPlaceholderText(tr("Quick select..."));
    m_quickSelectEdit->setFixedWidth(160);
    m_quickSelectEdit->installEventFilter(this);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_countsLabel);
    layout->addStretch();
    layout->addWidget(m_quickSelectEdit);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    connect(m_quickSelectEdit, &QLineEdit::textChanged, this, &StatusBarWidget::quickSelectTextChanged);
    connect(m_branchButton, &QToolButton::clicked, this, &StatusBarWidget::branchButtonClicked);

    setCounts(0, 0);
    updateGitGroupGeometry();
}

void StatusBarWidget::setCounts(int totalCount, int selectedCount)
{
    const QString totalText = totalCount == 1 ? tr("%1 item").arg(totalCount) : tr("%1 items").arg(totalCount);
    m_countsLabel->setText(selectedCount > 0 ? tr("%1, %2 selected").arg(totalText).arg(selectedCount) : totalText);
}

void StatusBarWidget::clearQuickSelect()
{
    m_quickSelectEdit->clear();
}

void StatusBarWidget::setGitInfo(bool inRepository, const QString& headDisplayName, int changedFileCount)
{
    m_branchButton->setVisible(inRepository);
    m_changedCountLabel->setVisible(inRepository);

    if (inRepository)
    {
        m_branchButton->setText(headDisplayName);
        m_branchButton->setToolTip(headDisplayName);
        m_changedCountLabel->setText(QString::number(changedFileCount));
    }

    // The group's sizeHint changes with the branch name/count text (and visibility), so its
    // centered position needs recomputing here, not just on resizeEvent.
    updateGitGroupGeometry();
}

void StatusBarWidget::showBranchMenu(const QStringList& branchNames, const QString& currentBranchName)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    for (const QString& branchName : branchNames)
    {
        QAction* action = menu->addAction(branchName);
        action->setCheckable(true);
        action->setChecked(branchName == currentBranchName);
        connect(action, &QAction::triggered, this, [this, branchName]() { emit branchSelected(branchName); });
    }

    // Anchored above the button, not below -- the status bar sits at the bottom of the window, so
    // a menu opening downward would frequently be clipped by the window edge.
    const QPoint topLeft = m_branchButton->mapToGlobal(m_branchButton->rect().topLeft());
    menu->popup(QPoint(topLeft.x(), topLeft.y() - menu->sizeHint().height()));
}

bool StatusBarWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_quickSelectEdit && event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape)
        {
            clearQuickSelect();
            emit quickSelectCancelled();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void StatusBarWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateGitGroupGeometry();
}

void StatusBarWidget::updateGitGroupGeometry()
{
    const QSize groupSize = m_gitGroup->sizeHint();
    const int x = (width() - groupSize.width()) / 2;
    const int y = (height() - groupSize.height()) / 2;
    m_gitGroup->setGeometry(qMax(x, 0), qMax(y, 0), groupSize.width(), groupSize.height());
}
