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

    m_imageLabel = new QLabel(m_stack);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_imageLabel);

    m_textView = new QPlainTextEdit(m_stack);
    m_textView->setReadOnly(true);
    QFont monospaceFont("Consolas");
    monospaceFont.setStyleHint(QFont::Monospace);
    m_textView->setFont(monospaceFont);
    m_stack->addWidget(m_textView);

    m_syntaxHighlighter = new SyntaxHighlighter(m_textView->document(), syntaxHighlightEngine, highlightThemeViewModel);

    m_folderList = new QListView(m_stack);
    m_folderList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_folderModel = new FolderPreviewListModel(this);
    m_folderList->setModel(m_folderModel);
    m_stack->addWidget(m_folderList);

    m_loadingLabel = new QLabel(tr("Loading..."), m_stack);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_loadingLabel);

    m_unsupportedLabel = new QLabel(tr("No preview available."), m_stack);
    m_unsupportedLabel->setAlignment(Qt::AlignCenter);
    m_unsupportedLabel->setWordWrap(true);
    m_stack->addWidget(m_unsupportedLabel);

    m_errorLabel = new QLabel(m_stack);
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

    rescaleImageLabel();
    m_stack->setCurrentWidget(m_imageLabel);
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

    m_imageLabel->setPixmap(m_currentImage.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
