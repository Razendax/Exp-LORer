#pragma once

#include <QWidget>

#include "NotificationType.h"

class QTimer;

// One popup (toast) notification card: a colored header strip (type icon + type name) over a
// word-wrapped body QLabel, plus an "x" close button. Auto-dismisses after timeoutMs via an owned
// QTimer (timeoutMs == 0 means never auto-dismiss -- close button only). Purely a presentation
// widget; NotificationOverlayWidget owns the lifetime and removes it from the stack on
// closeRequested().
class ToastNotificationWidget : public QWidget
{
    Q_OBJECT

public:
    ToastNotificationWidget(NotificationType type, const QString& title, const QString& body, int timeoutMs,
                             QWidget* parent = nullptr);

signals:
    void closeRequested();

private:
    QTimer* m_dismissTimer = nullptr;
};
