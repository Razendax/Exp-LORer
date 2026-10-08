#include "ToastNotificationWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiColors.h"

namespace
{
    QString headerColorFor(NotificationType type)
    {
        const UiColors::UiColorSet& colors = UiColors::current();
        switch (type)
        {
            case NotificationType::Info:
                return QString::fromLatin1(colors.notificationInfoBlue);
            case NotificationType::Warning:
                return QString::fromLatin1(colors.notificationWarningYellow);
            case NotificationType::Error:
                return QString::fromLatin1(colors.notificationErrorRed);
        }
        return QString();
    }

    QStyle::StandardPixmap iconFor(NotificationType type)
    {
        switch (type)
        {
            case NotificationType::Info:
                return QStyle::SP_MessageBoxInformation;
            case NotificationType::Warning:
                return QStyle::SP_MessageBoxWarning;
            case NotificationType::Error:
                return QStyle::SP_MessageBoxCritical;
        }
        return QStyle::SP_MessageBoxInformation;
    }

    QString typeName(NotificationType type)
    {
        switch (type)
        {
            case NotificationType::Info:
                return QObject::tr("Info");
            case NotificationType::Warning:
                return QObject::tr("Warning");
            case NotificationType::Error:
                return QObject::tr("Error");
        }
        return QString();
    }
}

ToastNotificationWidget::ToastNotificationWidget(NotificationType type, const QString& title, const QString& body, int timeoutMs,
                                                   QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("toastNotificationWidget"));
    // palette(...) roles rather than literal hex -- Light/Dark need no notifier wiring here since
    // this card is short-lived (auto-dismissed or closed by the user) and its colors are re-read
    // from the QPalette QApplication::setPalette() last installed on every paint (Architecture.md
    // §14.50).
    setStyleSheet(QStringLiteral("#toastNotificationWidget { background-color: palette(base); border: 1px solid palette(mid); }"));

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("toastHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setStyleSheet(QStringLiteral("#toastHeader { background-color: %1; }").arg(headerColorFor(type)));

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 4, 4);

    auto* iconLabel = new QLabel(header);
    iconLabel->setPixmap(style()->standardIcon(iconFor(type)).pixmap(16, 16));
    headerLayout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(typeName(type) + QStringLiteral(": ") + title, header);
    titleLabel->setStyleSheet(QStringLiteral("color: white; font-weight: bold; background: transparent;"));
    headerLayout->addWidget(titleLabel, 1);

    auto* closeButton = new QToolButton(header);
    closeButton->setObjectName(QStringLiteral("toastCloseButton"));
    closeButton->setText(QStringLiteral("x"));
    closeButton->setAutoRaise(true);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(QStringLiteral("color: white; background: transparent; font-weight: bold;"));
    connect(closeButton, &QToolButton::clicked, this, &ToastNotificationWidget::closeRequested);
    headerLayout->addWidget(closeButton);

    outerLayout->addWidget(header);

    auto* bodyLabel = new QLabel(body, this);
    bodyLabel->setWordWrap(true);
    bodyLabel->setContentsMargins(8, 6, 8, 6);
    outerLayout->addWidget(bodyLabel);

    if (timeoutMs > 0)
    {
        m_dismissTimer = new QTimer(this);
        m_dismissTimer->setSingleShot(true);
        connect(m_dismissTimer, &QTimer::timeout, this, &ToastNotificationWidget::closeRequested);
        m_dismissTimer->start(timeoutMs);
    }
}
