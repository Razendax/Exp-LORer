#include "ProgressNotificationWidget.h"

#include <algorithm>

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ProgressNotificationWidget::ProgressNotificationWidget(const QString& title, int barCount, bool cancellable, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("progressNotificationWidget"));
    // palette(...) roles rather than literal hex -- see ToastNotificationWidget's equivalent line
    // for why this needs no UiThemeChangeNotifier wiring (Architecture.md §14.50).
    setStyleSheet(QStringLiteral("#progressNotificationWidget { background-color: palette(base); border: 1px solid palette(mid); }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addWidget(titleLabel);

    m_bars.reserve(static_cast<size_t>(std::max(barCount, 0)));
    for (int i = 0; i < barCount; ++i)
    {
        auto* aboveLabel = new QLabel(this);
        layout->addWidget(aboveLabel);

        auto* progressBar = new QProgressBar(this);
        progressBar->setRange(0, 0);
        progressBar->setValue(0);
        layout->addWidget(progressBar);

        auto* progressLabel = new QLabel(this);
        progressLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 9px;"));
        layout->addWidget(progressLabel);

        auto* belowLabel = new QLabel(this);
        belowLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 9px;"));
        layout->addWidget(belowLabel);

        m_bars.push_back(BarRow{ aboveLabel, progressBar, progressLabel, belowLabel });
    }

    if (cancellable)
    {
        m_cancelButton = new QPushButton(tr("Cancel"), this);
        layout->addWidget(m_cancelButton);
        connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
            // Disabled (not removed/hidden) immediately to prevent double-firing while the actual
            // termination -- confirmed by the worker thread, then terminateProgressNotification --
            // is still in flight.
            m_cancelButton->setEnabled(false);
            emit cancelRequested();
        });
    }
}

void ProgressNotificationWidget::updateBar(int barIndex, int current, int max, const QString& aboveText,
                                            const QString& progressText, const QString& belowText)
{
    if (barIndex < 0 || static_cast<size_t>(barIndex) >= m_bars.size())
    {
        return;
    }

    BarRow& row = m_bars[static_cast<size_t>(barIndex)];
    row.progressBar->setRange(0, max);
    row.progressBar->setValue(current);
    row.aboveLabel->setText(aboveText);
    row.progressLabel->setText(progressText);
    row.belowLabel->setText(belowText);
}
