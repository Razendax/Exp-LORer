#pragma once

#include <filesystem>

#include <QString>
#include <QWidget>

#include "ContentSearchOptions.h"
#include "TextEditorOptions.h"

class FilePreview;
class HighlightThemeViewModel;
class SyntaxHighlightEngine;
class TextEditorPanel;
class TextFileEditViewModel;
class QLabel;
class QToolButton;
class QVBoxLayout;

// The text page of the Preview panel's stack (Architecture.md §14.98, superseding TextPreviewWidget):
// a real editor rather than a bare read-only view. Opens every file read-only (populated from the
// same capped preview text TextPreviewWidget used, so plain browsing has zero behavior/performance
// change); "Edit" fetches and swaps in the file's full contents and flips the embedded
// TextEditorPanel editable, with Revert/Save controls and a Ctrl+S shortcut. One instance lives for
// the life of the Preview panel; its inner TextEditorPanel is torn down and rebuilt fresh on every
// setPreview() call targeting a new file (no leftover find-bar/cursor/undo state).
class TextEditPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    TextEditPreviewWidget(TextFileEditViewModel& textFileEditViewModel, SyntaxHighlightEngine& syntaxHighlightEngine,
                           HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent = nullptr);

    // No-op if `preview` targets the same path this widget is already showing AND that path is
    // currently open for editing -- see the .cpp for why (a live refresh re-delivering the same
    // file, e.g. right after this widget's own Save, must not kick the user out of edit mode).
    void setPreview(const FilePreview& preview);

    // Called first thing from PreviewPanelWidget::onPreviewLoading(), before the new target's own
    // content is fetched -- the latest point at which the *old* editor (if dirty) still exists,
    // since the pane's selection has already moved on by then. Prompts Save/Don't Save (no
    // Cancel -- there is nothing left to cancel back to) when the current editor is open for
    // editing and has unsaved changes; a no-op otherwise. Either way, the old TextEditorPanel is
    // then torn down the next time setPreview() rebuilds it.
    void prepareForTargetChange();

    // Scrolls the editor to `oneBasedLine` and parks the cursor there (Architecture.md §14.104's
    // Find-in-Files line jump). No-op if `m_editorPanel` doesn't exist yet (e.g. preview still
    // loading) -- PreviewPanelWidget::goToLine already only calls this once the target path matches
    // what's on screen, but construction order within setPreview() is not this widget's concern.
    void goToLine(int oneBasedLine);

    // Preview's content-search highlight layer (Architecture.md §14.105): caches `query`/`options`
    // and reapplies them to whatever editor is currently shown. An empty `query` clears the
    // highlight (used both when content search is inactive and when there is no focused tab).
    void setContentSearchHighlight(const QString& query, const TextMatchOptions& options);

    // Settings > General > Preview (Architecture.md §14.98): the default TextEditorOptions feature
    // set every freshly-opened preview editor is built with. Read fresh on every setPreview() call
    // rather than cached, matching every other Settings page's "takes effect on next use" posture.
    static TextEditorOptions defaultOptions();

    static constexpr const char* kLineNumbersEnabledSettingsKey = "Preview/LineNumbersEnabled";
    static constexpr const char* kFindEnabledSettingsKey = "Preview/FindEnabled";
    static constexpr const char* kGoToLineEnabledSettingsKey = "Preview/GoToLineEnabled";
    static constexpr const char* kMulticursorEnabledSettingsKey = "Preview/MulticursorEnabled";
    static constexpr const char* kHighlightSelectionOccurrencesEnabledSettingsKey =
        "Preview/HighlightSelectionOccurrencesEnabled";

    static bool lineNumbersEnabled();
    static bool findEnabled();
    static bool goToLineEnabled();
    static bool multicursorEnabled();
    static bool highlightSelectionOccurrencesEnabled();

private:
    void onEditClicked();
    void onRevertClicked();
    void onSaveClicked();
    void onLoadReady(const std::filesystem::path& path, const QString& content);
    void onLoadFailed(const std::filesystem::path& path, const QString& message);
    void onSaveSucceeded(const std::filesystem::path& path);
    void onSaveFailed(const std::filesystem::path& path, const QString& message);
    void onDocumentModificationChanged(bool modified);

    // Reapplies m_contentSearchQuery/m_contentSearchOptions to m_editorPanel's current editor --
    // called right after every setPlainText() call site (setPreview, onLoadReady, onRevertClicked)
    // so the highlight tracks the Preview's target file, Edit's full-content swap, and Revert, same
    // as the cached-state design TextEditorWidget::setContentSearchHighlight itself documents.
    void reapplyContentSearchHighlight();

    void showError(const QString& message);
    void clearError();

    TextFileEditViewModel& m_textFileEditViewModel;
    SyntaxHighlightEngine& m_syntaxHighlightEngine;
    HighlightThemeViewModel& m_highlightThemeViewModel;

    QVBoxLayout* m_layout = nullptr;
    QToolButton* m_editButton = nullptr;
    QToolButton* m_revertButton = nullptr;
    QToolButton* m_saveButton = nullptr;
    QLabel* m_errorLabel = nullptr;
    TextEditorPanel* m_editorPanel = nullptr;

    std::filesystem::path m_currentPath;
    // Cached so Revert can reload it without re-asking TextFileEditViewModel.
    QString m_lastLoadedContent;
    bool m_requestInFlight = false;

    // The options m_editorPanel was last constructed with. setPreview() reuses the existing panel
    // (just swapping its text) instead of destroying and reconstructing it when the new target is
    // still plain read-only browsing with identical options (same detected highlight language,
    // same Settings > Preview flags) -- rebuilding on every selection change, even across a folder
    // of files that all share the same extension, paid a full editor/highlighter rebuild for
    // nothing.
    TextEditorOptions m_currentPanelOptions;

    // Cached last-pushed content-search state (see setContentSearchHighlight), reapplied by
    // reapplyContentSearchHighlight() after every setPlainText() call site.
    QString m_contentSearchQuery;
    TextMatchOptions m_contentSearchOptions;
};
