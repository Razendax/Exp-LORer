#pragma once

#include "TextEditorOptions.h"

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVector>

class QTimer;
class QMouseEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QFocusEvent;
class TextEditorLineNumberArea;
class SyntaxHighlighter;
class SyntaxHighlightEngine;
class HighlightThemeViewModel;

// Base widget for Exp-LORer's own text editor (ToDo.md "Core editing" section) -- grown
// incrementally as editor features (line numbers, multi-cursor, syntax highlighting, etc.)
// are added. Currently covers multi-cursor editing (ToDo.md "Multiselection / multi-cursor")
// and a line-number gutter (ToDo.md "Line numbers gutter, current-line highlight" -- gutter
// only, current-line highlight is a separate increment).
class TextEditorWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    // highlightEngine/highlightThemeViewModel are only consulted when options.highlightLanguage
    // has a value; both default to nullptr for call sites (e.g. TextEditorOptionsDialog's "no
    // highlighting" default) that never request a language, matching this widget's own optional
    // relationship to the shared highlighting stack (Architecture.md §14.26) -- unlike
    // TextPreviewWidget, which always has them.
    explicit TextEditorWidget(const TextEditorOptions& options, QWidget* parent = nullptr,
                               SyntaxHighlightEngine* highlightEngine = nullptr,
                               HighlightThemeViewModel* highlightThemeViewModel = nullptr);

    // Set by TextEditorFindBar; merged with the secondary-cursor highlight list so neither
    // highlight source clobbers the other (see applyExtraSelections).
    void setExtraFindSelections(const QList<QTextEdit::ExtraSelection>& selections);

    // Called by TextEditorLineNumberArea (its sizeHint/paintEvent), same pattern as
    // TextEditorFindBar calling setExtraFindSelections above.
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    // Called by TextEditorGotoLineBar. Clamps oneBasedLine into [1, blockCount()], clears any
    // secondary cursors, and moves the primary cursor to that line's start, centered in view.
    void goToLine(int oneBasedLine);

    // Called by TextEditorFindBar (Select All Occurrences). No-op if matches is empty; otherwise
    // the first match becomes the primary selection and the rest become secondary cursors, so
    // subsequent typing replaces every occurrence at once.
    void selectAllOccurrences(const QVector<QTextCursor>& matches);

    // Runtime toggle (Architecture.md §14.97), unlike the construction-time-only TextEditorOptions::
    // readOnly this widget still also honors. Calls the inherited QPlainTextEdit::setReadOnly()
    // *and* updates m_readOnly together, since the multi-cursor path in
    // handleMultiCursorKeyPress bypasses QPlainTextEdit's own editing entirely and so has its own
    // separate m_readOnly guard that QPlainTextEdit::setReadOnly() alone would leave stale.
    void setReadOnly(bool readOnly);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void addSecondaryCursorAt(const QTextCursor& cursor);
    void addCursorOnAdjacentLine(bool below);
    void clearSecondaryCursors();
    bool hasSecondaryCursors() const;
    void mergeCollidingCursors();
    void updateSecondarySelectionHighlight();
    void updateCurrentLineHighlight();
    void applyExtraSelections();

    bool handleMultiCursorKeyPress(QKeyEvent* event);
    void insertAtAllCursors(const QString& text);
    void deleteAtAllCursors(bool forward, bool wholeWord = false);
    void moveAllCursors(QTextCursor::MoveOperation operation,
                         QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
    void copyOrCutAtAllCursors(bool cut);
    void pasteAtAllCursors();

    QVector<QTextCursor> m_secondaryCursors;
    QTimer* m_secondaryCaretBlinkTimer = nullptr;
    bool m_secondaryCaretVisible = true;
    bool m_extendingSecondarySelection = false;
    QList<QTextEdit::ExtraSelection> m_findSelections;
    QList<QTextEdit::ExtraSelection> m_secondarySelections;
    QList<QTextEdit::ExtraSelection> m_currentLineSelections;
    TextEditorLineNumberArea* m_lineNumberArea = nullptr;
    bool m_lineNumbersEnabled = true;
    bool m_multicursorEnabled = true;
    bool m_readOnly = false;
    SyntaxHighlighter* m_syntaxHighlighter = nullptr;
};
