#pragma once

#include <vector>

#include <QWidget>

class QLabel;
class QProgressBar;

// One progress-bar notification card: a title label plus N rows, each a text label over a
// QProgressBar. No manual close button -- per the notification system's design, a progress
// notification ends only via NotificationCenter::terminateProgressNotification, not a user
// action. Purely a presentation widget; NotificationOverlayWidget owns the lifetime.
class ProgressNotificationWidget : public QWidget
{
    Q_OBJECT

public:
    ProgressNotificationWidget(const QString& title, int barCount, QWidget* parent = nullptr);

    void updateBar(int barIndex, int current, int max, const QString& text);

private:
    struct BarRow
    {
        QLabel* textLabel = nullptr;
        QProgressBar* progressBar = nullptr;
    };

    std::vector<BarRow> m_bars;
};
