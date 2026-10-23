#pragma once

#include <filesystem>
#include <optional>

#include <QWidget>

#include "FilePreview.h"

class FilePreviewViewModel;
class FolderPreviewWidget;
class HighlightThemeViewModel;
class MediaPreviewWidget;
class SyntaxHighlightEngine;
class TextEditPreviewWidget;
class TextFileEditViewModel;
class QComboBox;
class QHideEvent;
class QLabel;
class QShowEvent;
class QStackedWidget;

// The Preview tab's content in the right panel (Architecture.md §14.25/§14.58/§14.67/§14.79/
// §14.85, Specification.md "Preview Panel"): a QStackedWidget switching between a dedicated
// widget per preview kind (MediaPreviewWidget for image/video poster frames, SVG, the inline
// video player, and the inline GIF player; TextPreviewWidget; FolderPreviewWidget) plus
// loading/unsupported/error states, driven by FilePreviewViewModel's signals. For Video previews,
// a combo above the stack lets the user pick "Static image" or "Video player"; a second,
// independent combo does the same for animated-GIF previews ("Static image" / "Animation") --
// both choices are session-only members, not persisted. Both the video and GIF players are owned
// by MediaPreviewWidget (not this widget) so that flipping either combo only swaps the content
// inside MediaPreviewWidget's own inner stack, keeping the filename/Properties/zoom chrome
// mounted the whole time -- see §14.85. Calls setPanelActive(true/false) from showEvent/hideEvent
// so no background generation happens while this tab isn't the visible one.
class PreviewPanelWidget : public QWidget
{
    Q_OBJECT

public:
    PreviewPanelWidget(FilePreviewViewModel* viewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                        HighlightThemeViewModel& highlightThemeViewModel, TextFileEditViewModel& textFileEditViewModel,
                        QWidget* parent = nullptr);

    // Find-in-Files line jump (Architecture.md §14.104): forwards to the text preview's editor only
    // when `path` matches whatever Text preview is currently on screen -- a stale/mismatched request
    // (the user navigated elsewhere again before this fired) is a no-op. Returns whether the jump
    // was actually performed, so a caller can tell a same-preview immediate jump apart from one that
    // still needs to wait for a future previewReady().
    bool goToLine(const std::filesystem::path& path, int oneBasedLine);

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
    void onGifModeChanged(int index);
    void releaseGifPlayerIfActive();
    void showGifPage();

    // Releases the video/GIF player (if active) and clears the corresponding mode combo/cached
    // preview -- shared by onPreviewLoading/onPreviewFailed/onPreviewCleared (which reset both)
    // and onPreviewReady (which resets only whichever one the new preview doesn't need).
    void resetVideoState();
    void resetGifState();

    FilePreviewViewModel* m_viewModel = nullptr;

    QComboBox* m_previewModeCombo = nullptr; // objectName "previewModeCombo"
    QComboBox* m_gifModeCombo = nullptr;     // objectName "gifModeCombo"
    QStackedWidget* m_stack = nullptr;
    MediaPreviewWidget* m_mediaPreviewWidget = nullptr;
    TextEditPreviewWidget* m_textPreviewWidget = nullptr;
    FolderPreviewWidget* m_folderPreviewWidget = nullptr;
    QLabel* m_loadingLabel = nullptr;
    QLabel* m_unsupportedLabel = nullptr;
    QLabel* m_errorLabel = nullptr;

    // Last-seen Video preview, kept so switching the mode combo re-renders without re-asking
    // FilePreviewViewModel.
    std::optional<FilePreview> m_currentVideoPreview;
    // Same idea for an animated-GIF preview (Architecture.md §14.79) -- a GIF is still kind
    // Image, so it's tracked independently from m_currentVideoPreview rather than reusing it.
    std::optional<FilePreview> m_currentGifPreview;

    // The path of whatever Text preview is currently on screen, if any -- lets goToLine() reject a
    // stale request for a target this panel has since navigated away from. Empty for every other
    // preview kind.
    std::filesystem::path m_currentTextPath;
};
