#pragma once

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVector>

class QTimer;
class QMouseEvent;
class QKeyEvent;
class QPaintEvent;
class QFocusEvent;

// Base widget for Exp-LORer's own text editor (ToDo.md "Core editing" section) -- grown
// incrementally as editor features (line numbers, multi-cursor, syntax highlighting, etc.)
// are added. Currently covers multi-cursor editing (ToDo.md "Multiselection / multi-cursor").
class TextEditorWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TextEditorWidget(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void addSecondaryCursorAt(const QTextCursor& cursor);
    void addCursorOnAdjacentLine(bool below);
    void clearSecondaryCursors();
    bool hasSecondaryCursors() const;
    void mergeCollidingCursors();
    void updateSecondarySelectionHighlight();

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
};
