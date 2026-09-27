#include "ProgressNotificationWidget.h"

#include <algorithm>

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

ProgressNotificationWidget::ProgressNotificationWidget(const QString& title, int barCount, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("progressNotificationWidget"));
    setStyleSheet(QStringLiteral("#progressNotificationWidget { background-color: #FFFFFF; border: 1px solid #A0A0A0; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addWidget(titleLabel);

    m_bars.reserve(static_cast<size_t>(std::max(barCount, 0)));
    for (int i = 0; i < barCount; ++i)
    {
        auto* textLabel = new QLabel(this);
        layout->addWidget(textLabel);

        auto* progressBar = new QProgressBar(this);
        progressBar->setRange(0, 0);
        progressBar->setValue(0);
        layout->addWidget(progressBar);

        m_bars.push_back(BarRow{ textLabel, progressBar });
    }
}

void ProgressNotificationWidget::updateBar(int barIndex, int current, int max, const QString& text)
{
    if (barIndex < 0 || static_cast<size_t>(barIndex) >= m_bars.size())
    {
        return;
    }

    BarRow& row = m_bars[static_cast<size_t>(barIndex)];
    row.progressBar->setRange(0, max);
    row.progressBar->setValue(current);
    row.textLabel->setText(text);
}
