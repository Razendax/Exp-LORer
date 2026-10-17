#pragma once

#include <QVector>
#include <QTextCursor>
#include <QWidget>

class QLineEdit;
class QLabel;
class QToolButton;
class QEvent;
class TextEditorWidget;

// Find/replace bar for TextEditorWidget (ToDo.md "Core editing" -- find/replace), shown/hidden by
// TextEditorDialog via Ctrl+F. Floats as a small overlay anchored to the editor's top-right
// corner (VS Code-style) rather than taking a row in the dialog's layout, so it doesn't push the
// editor down or stretch across the full width. Owns the search algorithm and "what to search
// for and where the matches are"; TextEditorWidget itself only renders whatever extra selections
// it's handed (see TextEditorWidget::setExtraFindSelections). Find-in-selection is deliberately
// out of scope for this increment.
class TextEditorFindBar : public QWidget
{
    Q_OBJECT

public:
    // editor also becomes this widget's parent, so the find bar is a floating child drawn on top
    // of the editor's viewport instead of a widget in some enclosing layout.
    explicit TextEditorFindBar(TextEditorWidget* editor);

    // Shows the bar if hidden, repositions it over the editor, focuses the line edit and selects
    // its current text, then re-runs the search (so reopening with stale text immediately
    // re-highlights/re-counts).
    void showAndFocus();

    // Clears the editor's find highlights, hides the bar, and returns focus to the editor.
    void hideAndClear();

protected:
    // Installed on m_editor (to reposition this overlay when the editor is resized) and on the
    // search line edit (so Escape closes the bar while it has focus, without touching
    // TextEditorWidget::keyPressEvent's own Escape handling for secondary cursors).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Re-anchors this widget to the editor viewport's top-right corner, clear of the vertical
    // scrollbar. Called on construction, on showAndFocus, and whenever the editor resizes.
    void repositionOverEditor();


    // The one search routine, used by every trigger (text changed, option toggled, Next/
    // Previous, re-opened via Ctrl+F). backward selects search direction; fromScratch re-anchors
    // the "current match" search at the editor's own cursor/selection rather than continuing from
    // the previous match.
    void runSearch(bool backward, bool fromScratch);

    // Keeps Whole Word and Match Regex mutually exclusive: when one is checked, unchecks the
    // other (via QSignalBlocker, to avoid re-entrant runSearch calls) before re-running the search.
    void onMatchModeButtonToggled(QToolButton* button, bool checked);

    // Shows/hides the replace row's widgets in step with m_toggleReplaceButton's checked state,
    // updates the button's glyph, repositions the (now taller/shorter) overlay, and focuses
    // m_replaceEdit when expanding.
    void onToggleReplaceRow(bool checked);

    // Replaces m_currentMatch (computed via computeReplacement) and advances to the next match.
    // No-op if the find term is empty or there's no current match -- behaves like Next in that
    // case (see class-level Replace semantics).
    void onReplace();

    // Replaces every entry in m_matches in one undo step. No-op if m_matches is empty.
    void onReplaceAll();

    // Returns the text to substitute for match: m_replaceEdit's text verbatim when Match Regex is
    // off, otherwise match's selected text with the regex re-applied using a translated
    // replacement (see translateDollarCaptureRefs in the .cpp).
    QString computeReplacement(const QTextCursor& match) const;

    TextEditorWidget* m_editor = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_previousButton = nullptr;
    QToolButton* m_nextButton = nullptr;
    QToolButton* m_caseSensitiveButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QToolButton* m_toggleReplaceButton = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QToolButton* m_replaceButton = nullptr;
    QToolButton* m_replaceAllButton = nullptr;

    // Populated by runSearch on every return path (empty/null on the empty-term, invalid-regex,
    // and no-results early returns), so Replace/Replace All reuse the same match data instead of
    // re-deriving it.
    QVector<QTextCursor> m_matches;
    QTextCursor m_currentMatch;
};
