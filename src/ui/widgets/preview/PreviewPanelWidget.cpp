#include "PreviewPanelWidget.h"

#include <QHideEvent>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "FilePreviewViewModel.h"
#include "FolderPreviewWidget.h"
#include "MediaPreviewWidget.h"
#include "TextPreviewWidget.h"

PreviewPanelWidget::PreviewPanelWidget(FilePreviewViewModel* viewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                                        HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

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
}

void PreviewPanelWidget::onPreviewLoading()
{
    m_stack->setCurrentWidget(m_loadingLabel);
}

void PreviewPanelWidget::onPreviewReady(const FilePreview& preview)
{
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
    case FilePreviewKind::Video:
        if (m_mediaPreviewWidget->setPreview(preview))
            m_stack->setCurrentWidget(m_mediaPreviewWidget);
        else
            m_stack->setCurrentWidget(m_unsupportedLabel);
        break;
    case FilePreviewKind::Unsupported:
        m_stack->setCurrentWidget(m_unsupportedLabel);
        break;
    }
}

void PreviewPanelWidget::onPreviewFailed(const QString& message)
{
    m_errorLabel->setText(message);
    m_stack->setCurrentWidget(m_errorLabel);
}

void PreviewPanelWidget::onPreviewCleared()
{
    m_stack->setCurrentWidget(m_unsupportedLabel);
}
