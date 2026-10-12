#include "MediaPreviewWidget.h"

#include <algorithm>
#include <chrono>

#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTime>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "GifPlayerWidget.h"
#include "MediaMetadata.h"
#include "MediaThumbnailPainting.h"
#include "VideoPlayerWidget.h"

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

    m_contentStack = new QStackedWidget(this);
    m_contentStack->setObjectName(QStringLiteral("previewContentStack"));

    m_imageLabel = new QLabel(m_contentStack);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    // Without this, QLabel's sizeHint()/minimumSizeHint() track whatever pixmap was last set
    // (full prior panel size), so shrinking the right panel can't make incremental progress -- the
    // layout refuses to go below that stale hint until the resize pushes everything to the overall
    // minimum in one jump. Ignored + an explicit small minimum let the label (and rescaleImage(),
    // called from resizeEvent() below) track the splitter continuously in both directions.
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setMinimumSize(1, 1);
    m_contentStack->addWidget(m_imageLabel);

    m_videoPlayerWidget = new VideoPlayerWidget(m_contentStack);
    m_contentStack->addWidget(m_videoPlayerWidget);

    m_gifPlayerWidget = new GifPlayerWidget(m_contentStack);
    m_contentStack->addWidget(m_gifPlayerWidget);

    layout->addWidget(m_contentStack, 1);

    m_propertiesGroupBox = new QGroupBox(tr("Properties"), this);
    m_propertiesGroupBox->setObjectName(QStringLiteral("previewImagePropertiesGroupBox"));
    // Checkable turns the title into a collapse/expand toggle (Architecture.md §14.85); default
    // checked so it starts expanded, matching the box's always-shown behavior before this change.
    m_propertiesGroupBox->setCheckable(true);
    m_propertiesGroupBox->setChecked(true);
    auto* propertiesLayout = new QVBoxLayout(m_propertiesGroupBox);

    m_propertiesLabel = new QLabel(m_propertiesGroupBox);
    m_propertiesLabel->setObjectName(QStringLiteral("previewImagePropertiesLabel"));
    m_propertiesLabel->setWordWrap(true);
    // Same fixed-width rationale as m_nameLabel above: a long property value must not be able to
    // force the panel wider.
    m_propertiesLabel->setSizePolicy(QSizePolicy::Ignored, m_propertiesLabel->sizePolicy().verticalPolicy());
    propertiesLayout->addWidget(m_propertiesLabel);

    connect(m_propertiesGroupBox, &QGroupBox::toggled, m_propertiesLabel, &QLabel::setVisible);

    layout->addWidget(m_propertiesGroupBox);

    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setObjectName(QStringLiteral("previewZoomLabel"));
    layout->addWidget(m_zoomLabel);
}

void MediaPreviewWidget::setMetadata(const FilePreview& preview)
{
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

    updateZoomLabel();
}

bool MediaPreviewWidget::showStaticImage(const FilePreview& preview)
{
    const std::vector<std::byte>& bytes = preview.imageBytes();
    // No format hint: auto-detect from the byte signature so this still works for PNG-encoded SVG
    // thumbnails (Architecture.md §14.79) as well as the existing JPEG-encoded raster thumbnails.
    m_currentImage.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<uint>(bytes.size()));

    if (m_currentImage.isNull())
    {
        return false;
    }

    m_contentStack->setCurrentWidget(m_imageLabel);
    rescaleImage();
    return true;
}

void MediaPreviewWidget::showVideo(const std::filesystem::path& path)
{
    m_videoPlayerWidget->load(path);
    m_contentStack->setCurrentWidget(m_videoPlayerWidget);
}

void MediaPreviewWidget::showGifAnimation(const std::filesystem::path& path)
{
    m_gifPlayerWidget->load(path);
    m_contentStack->setCurrentWidget(m_gifPlayerWidget);
}

void MediaPreviewWidget::releaseVideoPlayer()
{
    m_videoPlayerWidget->release();
}

void MediaPreviewWidget::releaseGifPlayer()
{
    m_gifPlayerWidget->release();
}

void MediaPreviewWidget::releaseVideoPlayerIfActive()
{
    if (m_contentStack->currentWidget() == m_videoPlayerWidget)
        m_videoPlayerWidget->release();
}

void MediaPreviewWidget::releaseGifPlayerIfActive()
{
    if (m_contentStack->currentWidget() == m_gifPlayerWidget)
        m_gifPlayerWidget->release();
}

void MediaPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rescaleImage();
    updateZoomLabel();
}

void MediaPreviewWidget::rescaleImage()
{
    if (m_currentImage.isNull() || m_contentStack->currentWidget() != m_imageLabel)
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
}

void MediaPreviewWidget::updateZoomLabel()
{
    if (m_currentActualWidth <= 0 || m_currentActualHeight <= 0)
    {
        m_zoomLabel->setVisible(false);
        return;
    }

    // Driven by the content stack's own size rather than the static image label's rendered
    // pixmap, so the percentage stays live (and the label stays visible) across all three content
    // modes instead of only existing on the static-image page -- matching how both the image
    // label and GifPlayerWidget actually letterbox (Qt::KeepAspectRatio against their full area);
    // for the video player it's a slight overestimate since VideoPlayerWidget's own controls bar
    // eats into its available height, but that's preferable to the zoom label vanishing entirely.
    const QSize containerSize = m_contentStack->size();
    const double ratio = std::min(containerSize.width() / static_cast<double>(m_currentActualWidth),
                                   containerSize.height() / static_cast<double>(m_currentActualHeight));
    m_zoomLabel->setText(tr("Zoom: %1% (actual %2x%3)")
                              .arg(qRound(ratio * 100))
                              .arg(m_currentActualWidth)
                              .arg(m_currentActualHeight));
    m_zoomLabel->setVisible(true);
}
