#include "MediaPreviewWidget.h"

#include <chrono>

#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTime>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "MediaMetadata.h"
#include "MediaThumbnailPainting.h"

MediaPreviewWidget::MediaPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName(QStringLiteral("previewImageNameLabel"));
    m_nameLabel->setWordWrap(true);
    // A wrapped QLabel's minimumSizeHint() width is the widest *unbreakable* run of characters --
    // for a long filename with no spaces, that's the full unwrapped text width, which would force
    // the panel/splitter wider every time a long filename is previewed (and it never shrinks back).
    // Ignored horizontally drops that floor to 0 (see qSmartMinSize()) while still wrapping/eliding
    // within whatever width the layout actually gives it.
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, m_nameLabel->sizePolicy().verticalPolicy());
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    m_nameLabel->setFont(nameFont);
    layout->addWidget(m_nameLabel);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    // Without this, QLabel's sizeHint()/minimumSizeHint() track whatever pixmap was last set
    // (full prior panel size), so shrinking the right panel can't make incremental progress -- the
    // layout refuses to go below that stale hint until the resize pushes everything to the overall
    // minimum in one jump. Ignored + an explicit small minimum let the label (and rescaleImage(),
    // called from resizeEvent() below) track the splitter continuously in both directions.
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setMinimumSize(1, 1);
    layout->addWidget(m_imageLabel, 1);

    m_propertiesGroupBox = new QGroupBox(tr("Properties"), this);
    m_propertiesGroupBox->setObjectName(QStringLiteral("previewImagePropertiesGroupBox"));
    auto* propertiesLayout = new QVBoxLayout(m_propertiesGroupBox);

    m_propertiesLabel = new QLabel(m_propertiesGroupBox);
    m_propertiesLabel->setObjectName(QStringLiteral("previewImagePropertiesLabel"));
    m_propertiesLabel->setWordWrap(true);
    // Same fixed-width rationale as m_nameLabel above: a long property value must not be able to
    // force the panel wider.
    m_propertiesLabel->setSizePolicy(QSizePolicy::Ignored, m_propertiesLabel->sizePolicy().verticalPolicy());
    propertiesLayout->addWidget(m_propertiesLabel);

    layout->addWidget(m_propertiesGroupBox);

    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setObjectName(QStringLiteral("previewZoomLabel"));
    layout->addWidget(m_zoomLabel);
}

bool MediaPreviewWidget::setPreview(const FilePreview& preview)
{
    const std::vector<std::byte>& bytes = preview.imageBytes();
    // No format hint: auto-detect from the byte signature so this still works for PNG-encoded SVG
    // thumbnails (Architecture.md §14.79) as well as the existing JPEG-encoded raster thumbnails.
    m_currentImage.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<uint>(bytes.size()));

    if (m_currentImage.isNull())
    {
        return false;
    }

    m_nameLabel->setText(QString::fromStdWString(preview.path().filename().native()));

    const MediaMetadata& metadata = preview.metadata();
    m_currentActualWidth = metadata.width();
    m_currentActualHeight = metadata.height();

    QStringList properties;
    if (m_currentActualWidth > 0 && m_currentActualHeight > 0)
    {
        properties << tr("Resolution: %1x%2").arg(m_currentActualWidth).arg(m_currentActualHeight);
    }
    if (preview.kind() == FilePreviewKind::Video && metadata.duration().count() > 0)
    {
        const auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(metadata.duration()).count();
        const QTime time = QTime(0, 0).addSecs(static_cast<int>(totalSeconds));
        const QString format = totalSeconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss");
        properties << tr("Length: %1").arg(time.toString(format));
    }
    if (preview.kind() == FilePreviewKind::Video && metadata.bitRateBps() > 0)
    {
        properties << tr("Bitrate: %1 kbps").arg(metadata.bitRateBps() / 1000);
    }
    if (preview.fileSizeBytes() > 0)
    {
        properties << tr("Size: %1").arg(QLocale().formattedDataSize(static_cast<qint64>(preview.fileSizeBytes())));
    }
    m_propertiesLabel->setText(properties.join(QStringLiteral("\n")));
    m_propertiesGroupBox->setVisible(!properties.isEmpty());

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

    // A transparent SVG/GIF preview keeps its real alpha channel (Architecture.md §14.79/§14.81),
    // so this label's own background would otherwise show through it -- in a dark theme that
    // swallows dark content the same way it did for Media Viewer thumbnails before §14.82's fix,
    // which this mirrors for the Preview tab. Composited only behind `scaled` itself, not the
    // whole label, since the label aligns the pixmap top-centre within whatever extra space it has.
    if (scaled.hasAlphaChannel())
    {
        QPixmap withCheckerboard(scaled.size());
        QPainter painter(&withCheckerboard);
        painter.fillRect(withCheckerboard.rect(), MediaThumbnailPainting::checkerboardBrush());
        painter.drawPixmap(0, 0, scaled);
        painter.end();
        m_imageLabel->setPixmap(withCheckerboard);
    }
    else
    {
        m_imageLabel->setPixmap(scaled);
    }

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
