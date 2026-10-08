#include "MediaPreviewWidget.h"

#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "MediaMetadata.h"

MediaPreviewWidget::MediaPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("previewImageInfoLabel"));
    m_infoLabel->setWordWrap(true);
    layout->addWidget(m_infoLabel);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_imageLabel, 1);

    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setObjectName(QStringLiteral("previewZoomLabel"));
    layout->addWidget(m_zoomLabel);
}

bool MediaPreviewWidget::setPreview(const FilePreview& preview)
{
    const std::vector<std::byte>& bytes = preview.imageBytes();
    m_currentImage.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<uint>(bytes.size()), "JPG");

    if (m_currentImage.isNull())
    {
        return false;
    }

    const MediaMetadata& metadata = preview.metadata();
    m_currentActualWidth = metadata.width();
    m_currentActualHeight = metadata.height();

    if (m_currentActualWidth > 0 && m_currentActualHeight > 0)
    {
        QString info = QString::fromStdWString(preview.path().filename().native())
            + QStringLiteral("  —  %1x%2").arg(m_currentActualWidth).arg(m_currentActualHeight);
        if (preview.kind() == FilePreviewKind::Video && metadata.bitRateBps() > 0)
        {
            info += QStringLiteral(", %1 kbps").arg(metadata.bitRateBps() / 1000);
        }
        m_infoLabel->setText(info);
        m_infoLabel->setVisible(true);
    }
    else
    {
        m_infoLabel->setVisible(false);
    }

    rescaleImage();
    return true;
}

void MediaPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rescaleImage();
}

void MediaPreviewWidget::rescaleImage()
{
    if (m_currentImage.isNull())
    {
        return;
    }

    const QPixmap scaled = m_currentImage.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);

    if (m_currentActualWidth > 0)
    {
        const double ratio = scaled.width() / static_cast<double>(m_currentActualWidth);
        m_zoomLabel->setText(tr("Zoom: %1% (actual %2x%3)")
                                  .arg(qRound(ratio * 100))
                                  .arg(m_currentActualWidth)
                                  .arg(m_currentActualHeight));
        m_zoomLabel->setVisible(true);
    }
    else
    {
        m_zoomLabel->setVisible(false);
    }
}
