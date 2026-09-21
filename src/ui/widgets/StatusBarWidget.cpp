#include "StatusBarWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>

StatusBarWidget::StatusBarWidget(QWidget* parent)
    : QWidget(parent)
{
    m_countsLabel = new QLabel(this);

    m_quickSelectEdit = new QLineEdit(this);
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

    setCounts(0, 0);
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
