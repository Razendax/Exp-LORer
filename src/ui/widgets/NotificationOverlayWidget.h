#pragma once

#include <QHash>
#include <QWidget>

#include "NotificationType.h"

class NotificationCenter;
class QVBoxLayout;
class ProgressNotificationWidget;

// A plain child of MainWindow (not the central widget), always raised above it, holding a
// bottom-aligned stack of ToastNotificationWidget/ProgressNotificationWidget cards in creation
// order (newest nearest the bottom-right corner). Subscribes to NotificationCenter's four signals
// to create/update/destroy cards; a card's own closeRequested()/dismiss-timer removes just that
// card and lets the layout reflow. MainWindow repositions this via reposition() on every
// resizeEvent. Untested per Architecture.md §11's UI/ViewModel-glue carve-out.
class NotificationOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationOverlayWidget(NotificationCenter& notificationCenter, QWidget* parent);

    // Anchors this overlay to the parent's bottom-right corner: a fixed width, `availableHeight`
    // tall (the parent's client height minus e.g. the status bar), and raised above every sibling.
    void reposition(int availableHeight);

private:
    void onPopupRequested(NotificationType type, const QString& title, const QString& body, int timeoutMs);
    void onProgressNotificationCreated(int id, const QString& title, int barCount);
    void onProgressBarUpdated(int id, int barIndex, int current, int max, const QString& text);
    void onProgressNotificationTerminated(int id);

    static constexpr int kWidth = 320;

    NotificationCenter& m_notificationCenter;
    QVBoxLayout* m_stackLayout = nullptr;
    QHash<int, ProgressNotificationWidget*> m_progressWidgets;
};
