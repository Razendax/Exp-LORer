#include "PreviewPanelWidget.h"

#include <QComboBox>
#include <QHideEvent>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "FilePreviewViewModel.h"
#include "FolderPreviewWidget.h"
#include "MediaExtensions.h"
#include "MediaPreviewWidget.h"
#include "TextPreviewWidget.h"

PreviewPanelWidget::PreviewPanelWidget(FilePreviewViewModel* viewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                                        HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_previewModeCombo = new QComboBox(this);
    m_previewModeCombo->setObjectName(QStringLiteral("previewModeCombo"));
    m_previewModeCombo->addItem(tr("Static image"));
    m_previewModeCombo->addItem(tr("Video player"));
    m_previewModeCombo->hide();
    layout->addWidget(m_previewModeCombo);

    m_gifModeCombo = new QComboBox(this);
    m_gifModeCombo->setObjectName(QStringLiteral("gifModeCombo"));
    m_gifModeCombo->addItem(tr("Static image"));
    m_gifModeCombo->addItem(tr("Animation"));
    m_gifModeCombo->hide();
    layout->addWidget(m_gifModeCombo);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_mediaPreviewWidget = new MediaPreviewWidget(m_stack);
    m_stack->addWidget(m_mediaPreviewWidget);

    m_textPreviewWidget = new TextPreviewWidget(syntaxHighlightEngine, highlightThemeViewModel, m_stack);
    m_stack->addWidget(m_textPreviewWidget);

    m_folderPreviewWidget = new FolderPreviewWidget(m_stack);
    m_stack->addWidget(m_folderPreviewWidget);

    m_loadingLabel = new QLabel(tr("Loading..."), m_stack);
    m_loadingLabel->setObjectName(QStringLiteral("previewLoadingLabel"));
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_loadingLabel);

    m_unsupportedLabel = new QLabel(tr("No preview available."), m_stack);
    m_unsupportedLabel->setObjectName(QStringLiteral("previewUnsupportedLabel"));
    m_unsupportedLabel->setAlignment(Qt::AlignCenter);
    m_unsupportedLabel->setWordWrap(true);
    m_stack->addWidget(m_unsupportedLabel);

    m_errorLabel = new QLabel(m_stack);
    m_errorLabel->setObjectName(QStringLiteral("previewErrorLabel"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    m_stack->addWidget(m_errorLabel);

    m_stack->setCurrentWidget(m_unsupportedLabel);

    connect(m_viewModel, &FilePreviewViewModel::previewLoading, this, &PreviewPanelWidget::onPreviewLoading);
    connect(m_viewModel, &FilePreviewViewModel::previewReady, this, &PreviewPanelWidget::onPreviewReady);
    connect(m_viewModel, &FilePreviewViewModel::previewFailed, this, &PreviewPanelWidget::onPreviewFailed);
    connect(m_viewModel, &FilePreviewViewModel::previewCleared, this, &PreviewPanelWidget::onPreviewCleared);
    connect(m_previewModeCombo, &QComboBox::currentIndexChanged, this, &PreviewPanelWidget::onPreviewModeChanged);
    connect(m_gifModeCombo, &QComboBox::currentIndexChanged, this, &PreviewPanelWidget::onGifModeChanged);
}

void PreviewPanelWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_viewModel->setPanelActive(true);
}

void PreviewPanelWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_viewModel->setPanelActive(false);
    m_mediaPreviewWidget->releaseVideoPlayer();
    m_mediaPreviewWidget->releaseGifPlayer();
}

void PreviewPanelWidget::onPreviewLoading()
{
    m_mediaPreviewWidget->releaseVideoPlayer();
    m_previewModeCombo->hide();
    m_currentVideoPreview.reset();
    m_mediaPreviewWidget->releaseGifPlayer();
    m_gifModeCombo->hide();
    m_currentGifPreview.reset();
    m_stack->setCurrentWidget(m_loadingLabel);
}

void PreviewPanelWidget::onPreviewReady(const FilePreview& preview)
{
    if (preview.kind() != FilePreviewKind::Video)
    {
        m_mediaPreviewWidget->releaseVideoPlayer();
        m_previewModeCombo->hide();
        m_currentVideoPreview.reset();
    }

    const bool isAnimatedImage = preview.kind() == FilePreviewKind::Image
        && MediaExtensions::isAnimatedImageExtension(MediaExtensions::lowercaseExtension(preview.path()));
    if (!isAnimatedImage)
    {
        m_mediaPreviewWidget->releaseGifPlayer();
        m_gifModeCombo->hide();
        m_currentGifPreview.reset();
    }

    switch (preview.kind())
    {
    case FilePreviewKind::Folder:
        m_folderPreviewWidget->setPreview(preview);
        m_stack->setCurrentWidget(m_folderPreviewWidget);
        break;
    case FilePreviewKind::Text:
        m_textPreviewWidget->setPreview(preview);
        m_stack->setCurrentWidget(m_textPreviewWidget);
        break;
    case FilePreviewKind::Image:
        m_mediaPreviewWidget->setMetadata(preview);
        if (isAnimatedImage)
        {
            m_currentGifPreview = preview;
            m_gifModeCombo->show();
            showGifPage();
        }
        else if (m_mediaPreviewWidget->showStaticImage(preview))
            m_stack->setCurrentWidget(m_mediaPreviewWidget);
        else
            m_stack->setCurrentWidget(m_unsupportedLabel);
        break;
    case FilePreviewKind::Video:
        m_mediaPreviewWidget->setMetadata(preview);
        m_currentVideoPreview = preview;
        m_previewModeCombo->show();
        showVideoPage();
        break;
    case FilePreviewKind::Unsupported:
        m_stack->setCurrentWidget(m_unsupportedLabel);
        break;
    }
}

void PreviewPanelWidget::onPreviewFailed(const QString& message)
{
    m_mediaPreviewWidget->releaseVideoPlayer();
    m_previewModeCombo->hide();
    m_currentVideoPreview.reset();
    m_mediaPreviewWidget->releaseGifPlayer();
    m_gifModeCombo->hide();
    m_currentGifPreview.reset();
    m_errorLabel->setText(message);
    m_stack->setCurrentWidget(m_errorLabel);
}

void PreviewPanelWidget::onPreviewCleared()
{
    m_mediaPreviewWidget->releaseVideoPlayer();
    m_previewModeCombo->hide();
    m_currentVideoPreview.reset();
    m_mediaPreviewWidget->releaseGifPlayer();
    m_gifModeCombo->hide();
    m_currentGifPreview.reset();
    m_stack->setCurrentWidget(m_unsupportedLabel);
}

void PreviewPanelWidget::onPreviewModeChanged(int /*index*/)
{
    if (!m_currentVideoPreview)
        return;

    releaseVideoPlayerIfActive();
    showVideoPage();
}

void PreviewPanelWidget::releaseVideoPlayerIfActive()
{
    m_mediaPreviewWidget->releaseVideoPlayerIfActive();
}

void PreviewPanelWidget::showVideoPage()
{
    if (m_previewModeCombo->currentIndex() == 1)
    {
        m_mediaPreviewWidget->showVideo(m_currentVideoPreview->path());
        m_stack->setCurrentWidget(m_mediaPreviewWidget);
    }
    else if (m_mediaPreviewWidget->showStaticImage(*m_currentVideoPreview))
    {
        m_stack->setCurrentWidget(m_mediaPreviewWidget);
    }
    else
    {
        m_stack->setCurrentWidget(m_unsupportedLabel);
    }
}

void PreviewPanelWidget::onGifModeChanged(int /*index*/)
{
    if (!m_currentGifPreview)
        return;

    releaseGifPlayerIfActive();
    showGifPage();
}

void PreviewPanelWidget::releaseGifPlayerIfActive()
{
    m_mediaPreviewWidget->releaseGifPlayerIfActive();
}

void PreviewPanelWidget::showGifPage()
{
    if (m_gifModeCombo->currentIndex() == 1)
    {
        m_mediaPreviewWidget->showGifAnimation(m_currentGifPreview->path());
        m_stack->setCurrentWidget(m_mediaPreviewWidget);
    }
    else if (m_mediaPreviewWidget->showStaticImage(*m_currentGifPreview))
    {
        m_stack->setCurrentWidget(m_mediaPreviewWidget);
    }
    else
    {
        m_stack->setCurrentWidget(m_unsupportedLabel);
    }
}
