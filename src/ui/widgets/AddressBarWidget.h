#pragma once

#include <filesystem>

#include <QLineEdit>
#include <QStringList>

class QListWidget;
class QTimer;
class QEvent;
class QKeyEvent;
class QFocusEvent;

// Drop-in replacement for a bare QLineEdit used as a pane's address bar: while typing, shows a
// popup listing the folders (never files) that match what's typed so far, directly under the
// parent directory implied by the typed text, with keyboard/mouse navigation and inline
// autocomplete of an unambiguous common prefix. Self-contained: owns the popup, the debounce
// timer, and the per-directory suggestion cache; has no FileNavigationUseCase/TabViewModel
// dependency -- the owning WorkspacePaneWidget supplies suggestions via setSuggestions() in
// response to folderSuggestionsRequested.
class AddressBarWidget : public QLineEdit
{
    Q_OBJECT

public:
    explicit AddressBarWidget(QWidget* parent = nullptr);

public slots:
    // Response to the most recent folderSuggestionsRequested. Stale-response guarded: ignored if
    // the directory portion of the text has since changed again.
    void setSuggestions(const std::filesystem::path& directory, const QStringList& folderNames);

signals:
    // Emitted (debounced ~120ms) only when the *directory* portion of the typed text changes
    // (cache miss) -- not on every keystroke.
    void folderSuggestionsRequested(const std::filesystem::path& directory);

protected:
    // Overridden (rather than just keyPressEvent) because QWidget::event() intercepts a bare Tab
    // press for focus-chain traversal *before* keyPressEvent ever runs -- keyPressEvent alone
    // cannot claim Tab away from that default "move to next widget" behavior.
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void onTextEdited(const QString& text);
    void requestSuggestionsIfNeeded();
    void refreshPopup();
    void hidePopup();
    void applyInlineCompletion(const QStringList& matches, const QString& typedPrefix);
    void acceptCurrentSuggestion();
    // Commits whatever completion is pending (a highlighted popup row, or an unaccepted ghosted
    // suffix) into plain, non-ghosted text without navigating. Returns false if there was nothing
    // to commit, so Tab can fall back to normal focus-chain traversal.
    bool acceptPendingCompletion();
    void moveHighlight(int delta);

    static std::filesystem::path directoryPortion(const QString& text);
    static QString prefixPortion(const QString& text);

    QListWidget* m_popup = nullptr;
    QTimer* m_debounceTimer = nullptr;

    std::filesystem::path m_pendingDirectory;   // directory to request suggestions for once the debounce fires
    std::filesystem::path m_cachedDirectory;    // directory the last received suggestions are for
    QStringList m_cachedFolderNames;            // unfiltered listing of m_cachedDirectory
    bool m_haveCachedDirectory = false;

    // Set from the actual key in keyPressEvent (Backspace/Delete), *not* inferred from a text-
    // length comparison -- typing a character over a selected ghost suffix also shrinks the text
    // relative to what was last displayed, which is not a deletion and must still re-ghost.
    bool m_lastEditWasDeletion = false;
};
