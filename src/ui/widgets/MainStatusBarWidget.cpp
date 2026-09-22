#include "MainStatusBarWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSysInfo>

MainStatusBarWidget::MainStatusBarWidget(QWidget* parent)
    : QWidget(parent)
{
    m_driveLabel = new QLabel(this);
    m_computerNameLabel = new QLabel(QSysInfo::machineHostName(), this);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_driveLabel);
    layout->addStretch();
    layout->addWidget(m_computerNameLabel);
    layout->addStretch();

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void MainStatusBarWidget::setDriveInfoText(const QString& text)
{
    m_driveLabel->setText(text);
}
