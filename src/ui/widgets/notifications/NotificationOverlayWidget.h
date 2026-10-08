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
// card and lets the layout reflow. Shrink-wrapped to exactly the height of its current cards (not
// the parent's full client height) and re-anchored to the bottom-right corner on every card
// add/remove, so the widget's own geometry never covers empty space -- deliberately NOT
// Qt::WA_TransparentForMouseEvents: that attribute removes the whole subtree (cards included)
// from mouse hit-testing, not just the widget's own background, which would make every card's
// buttons (e.g. ProgressNotificationWidget's Cancel) unclickable. MainWindow repositions this via
// reposition() on every resizeEvent. Untested per Architecture.md §11's UI/ViewModel-glue carve-out.
class NotificationOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationOverlayWidget(NotificationCenter& notificationCenter, QWidget* parent);

    // Records the parent's available client height (its height minus e.g. the status bar) and
    // re-anchors this overlay to the bottom-right corner within it.
    void reposition(int availableHeight);

private:
    void onPopupRequested(NotificationType type, const QString& title, const QString& body, int timeoutMs);
    void onProgressNotificationCreated(int id, const QString& title, int barCount, bool cancellable);
    void onProgressBarUpdated(int id, int barIndex, int current, int max, const QString& aboveText, const QString& progressText,
                               const QString& belowText);
    void onProgressNotificationTerminated(int id);

    // Resizes/repositions this widget to a fixed width and a height matching its current cards
    // (clamped to m_availableHeight), with its bottom edge pinned to m_availableHeight.
    void updateGeometry();

    static constexpr int kWidth = 320;

    NotificationCenter& m_notificationCenter;
    QVBoxLayout* m_stackLayout = nullptr;
    QHash<int, ProgressNotificationWidget*> m_progressWidgets;
    int m_availableHeight = 0;
};
