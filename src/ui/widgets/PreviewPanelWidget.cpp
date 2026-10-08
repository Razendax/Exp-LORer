#include "PreviewPanelWidget.h"

#include <QFont>
#include <QHideEvent>
#include <QLabel>
#include <QListView>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "FilePreviewViewModel.h"
#include "FolderPreviewListModel.h"
#include "LanguageRegistry.h"
#include "MediaExtensions.h"
#include "MediaMetadata.h"
#include "SyntaxHighlighter.h"

PreviewPanelWidget::PreviewPanelWidget(FilePreviewViewModel* viewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                                        HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent)
    : QWidget(parent)
    , m_viewModel(viewModel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_imagePreviewPage = new QWidget(m_stack);
    auto* imagePageLayout = new QVBoxLayout(m_imagePreviewPage);
    imagePageLayout->setContentsMargins(0, 0, 0, 0);

    m_imageInfoLabel = new QLabel(m_imagePreviewPage);
    m_imageInfoLabel->setObjectName(QStringLiteral("previewImageInfoLabel"));
    m_imageInfoLabel->setWordWrap(true);
    imagePageLayout->addWidget(m_imageInfoLabel);

    m_imageLabel = new QLabel(m_imagePreviewPage);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    imagePageLayout->addWidget(m_imageLabel, 1);

    m_zoomLabel = new QLabel(m_imagePreviewPage);
    m_zoomLabel->setObjectName(QStringLiteral("previewZoomLabel"));
    imagePageLayout->addWidget(m_zoomLabel);

    m_stack->addWidget(m_imagePreviewPage);

    m_textView = new QPlainTextEdit(m_stack);
    m_textView->setObjectName(QStringLiteral("previewTextView"));
    m_textView->setReadOnly(true);
    QFont monospaceFont("Consolas");
    monospaceFont.setStyleHint(QFont::Monospace);
    m_textView->setFont(monospaceFont);
    m_stack->addWidget(m_textView);

    m_syntaxHighlighter = new SyntaxHighlighter(m_textView->document(), syntaxHighlightEngine, highlightThemeViewModel);

    m_folderList = new QListView(m_stack);
    m_folderList->setObjectName(QStringLiteral("previewFolderList"));
    m_folderList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_folderModel = new FolderPreviewListModel(this);
    m_folderList->setModel(m_folderModel);
    m_stack->addWidget(m_folderList);

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

void PreviewPanelWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rescaleImageLabel();
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
        showFolder(preview);
        break;
    case FilePreviewKind::Text:
        showText(preview);
        break;
    case FilePreviewKind::Image:
    case FilePreviewKind::Video:
        showImage(preview);
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

void PreviewPanelWidget::showImage(const FilePreview& preview)
{
    const std::vector<std::byte>& bytes = preview.imageBytes();
    m_currentImage.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<uint>(bytes.size()), "JPG");

    if (m_currentImage.isNull())
    {
        m_stack->setCurrentWidget(m_unsupportedLabel);
        return;
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
        m_imageInfoLabel->setText(info);
        m_imageInfoLabel->setVisible(true);
    }
    else
    {
        m_imageInfoLabel->setVisible(false);
    }

    rescaleImageLabel();
    m_stack->setCurrentWidget(m_imagePreviewPage);
}

void PreviewPanelWidget::showText(const FilePreview& preview)
{
    QString content = QString::fromUtf8(preview.text().data(), static_cast<int>(preview.text().size()));
    if (preview.textTruncated())
    {
        content += tr("\n\n[...truncated...]");
    }

    // QSyntaxHighlighter reformats automatically on every QTextDocument::contentsChange, so
    // setPlainText() below would otherwise trigger one highlightBlock pass using whatever language
    // the highlighter was still holding from the previous preview (a visible flash of wrong-language
    // highlighting, and wasted work). Block the document's signals for that one call so the only
    // highlight pass is the correct one setLanguage() triggers explicitly right after, against both
    // the new text and the new language.
    m_textView->document()->blockSignals(true);
    m_textView->setPlainText(content);
    m_textView->document()->blockSignals(false);

    const std::string extension = MediaExtensions::lowercaseExtension(preview.path());
    m_syntaxHighlighter->setLanguage(LanguageRegistry::languageForExtension(extension));

    m_stack->setCurrentWidget(m_textView);
}

void PreviewPanelWidget::showFolder(const FilePreview& preview)
{
    m_folderModel->setEntries(preview.folderEntries());
    m_stack->setCurrentWidget(m_folderList);
}

void PreviewPanelWidget::rescaleImageLabel()
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
