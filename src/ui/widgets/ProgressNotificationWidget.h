#pragma once

#include <vector>

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

// One progress-bar notification card: a title label, N rows (a text label above and a smaller-
// font text label below one QProgressBar each), and an optional single Cancel button (the whole
// notification's -- this feature only ever uses barCount == 1, but the button logically cancels
// the whole notification, matching NotificationCenter::requestCancel(id)'s per-notification
// granularity, Architecture.md §14.47). No manual close button otherwise -- per the notification
// system's design, a non-cancellable progress notification ends only via
// NotificationCenter::terminateProgressNotification, never a user action. Purely a presentation
// widget; NotificationOverlayWidget owns the lifetime.
class ProgressNotificationWidget : public QWidget
{
    Q_OBJECT

public:
    ProgressNotificationWidget(const QString& title, int barCount, bool cancellable, QWidget* parent = nullptr);

    void updateBar(int barIndex, int current, int max, const QString& aboveText, const QString& progressText,
                   const QString& belowText);

signals:
    void cancelRequested();

private:
    struct BarRow
    {
        QLabel* aboveLabel = nullptr;
        QProgressBar* progressBar = nullptr;
        QLabel* progressLabel = nullptr;
        QLabel* belowLabel = nullptr;
    };

    std::vector<BarRow> m_bars;
    QPushButton* m_cancelButton = nullptr;
};
