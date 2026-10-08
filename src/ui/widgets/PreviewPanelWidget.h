#pragma once

#include <QWidget>

class FilePreview;
class FilePreviewViewModel;
class FolderPreviewWidget;
class HighlightThemeViewModel;
class MediaPreviewWidget;
class SyntaxHighlightEngine;
class TextPreviewWidget;
class QHideEvent;
class QLabel;
class QShowEvent;
class QStackedWidget;

// The Preview tab's content in the right panel (Architecture.md §14.25/§14.58, Specification.md
// "Preview Panel"): a QStackedWidget switching between a dedicated widget per preview kind
// (MediaPreviewWidget for image/video, TextPreviewWidget, FolderPreviewWidget) plus
// loading/unsupported/error states, driven by FilePreviewViewModel's signals. Calls
// setPanelActive(true/false) from showEvent/hideEvent so no background generation happens while
// this tab isn't the visible one.
class PreviewPanelWidget : public QWidget
{
    Q_OBJECT

public:
    PreviewPanelWidget(FilePreviewViewModel* viewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                        HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void onPreviewLoading();
    void onPreviewReady(const FilePreview& preview);
    void onPreviewFailed(const QString& message);
    void onPreviewCleared();

    FilePreviewViewModel* m_viewModel = nullptr;

    QStackedWidget* m_stack = nullptr;
    MediaPreviewWidget* m_mediaPreviewWidget = nullptr;
    TextPreviewWidget* m_textPreviewWidget = nullptr;
    FolderPreviewWidget* m_folderPreviewWidget = nullptr;
    QLabel* m_loadingLabel = nullptr;
    QLabel* m_unsupportedLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
};
