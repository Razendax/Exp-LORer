#pragma once

#include <optional>

#include <QWidget>

#include "FilePreview.h"

class FilePreviewViewModel;
class FolderPreviewWidget;
class HighlightThemeViewModel;
class MediaPreviewWidget;
class SyntaxHighlightEngine;
class TextPreviewWidget;
class VideoPlayerWidget;
class QComboBox;
class QHideEvent;
class QLabel;
class QShowEvent;
class QStackedWidget;

// The Preview tab's content in the right panel (Architecture.md §14.25/§14.58/§14.67,
// Specification.md "Preview Panel"): a QStackedWidget switching between a dedicated widget per
// preview kind (MediaPreviewWidget for image/video poster frames, TextPreviewWidget,
// FolderPreviewWidget, VideoPlayerWidget) plus loading/unsupported/error states, driven by
// FilePreviewViewModel's signals. For Video previews, a combo above the stack lets the user pick
// "Static image" (poster frame, MediaPreviewWidget) or "Video player" (VideoPlayerWidget); the
// choice is a session-only member, not persisted. Calls setPanelActive(true/false) from
// showEvent/hideEvent so no background generation happens while this tab isn't the visible one.
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
    void onPreviewModeChanged(int index);
    void releaseVideoPlayerIfActive();
    void showVideoPage();

    FilePreviewViewModel* m_viewModel = nullptr;

    QComboBox* m_previewModeCombo = nullptr; // objectName "previewModeCombo"
    QStackedWidget* m_stack = nullptr;
    MediaPreviewWidget* m_mediaPreviewWidget = nullptr;
    TextPreviewWidget* m_textPreviewWidget = nullptr;
    FolderPreviewWidget* m_folderPreviewWidget = nullptr;
    VideoPlayerWidget* m_videoPlayerWidget = nullptr;
    QLabel* m_loadingLabel = nullptr;
    QLabel* m_unsupportedLabel = nullptr;
    QLabel* m_errorLabel = nullptr;

    // Last-seen Video preview, kept so switching the mode combo re-renders without re-asking
    // FilePreviewViewModel.
    std::optional<FilePreview> m_currentVideoPreview;
};
