#pragma once

#include <QPlainTextEdit>

// Base widget for Exp-LORer's own text editor (ToDo.md "Core editing" section) -- currently a
// thin QPlainTextEdit subclass with no behavior of its own, grown incrementally as editor
// features (line numbers, multi-cursor, syntax highlighting, etc.) are added.
class TextEditorWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TextEditorWidget(QWidget* parent = nullptr);
};
