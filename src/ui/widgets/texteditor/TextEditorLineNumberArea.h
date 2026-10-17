#pragma once

#include <QWidget>

class QPaintEvent;
class TextEditorWidget;

// Thin gutter widget that just forwards sizing/painting to its owning TextEditorWidget, which
// owns the actual line-number layout logic (mirrors the Qt "Code Editor" example's
// LineNumberArea, adapted to this project's TextEditorFindBar-style "small companion QWidget"
// precedent rather than a nested private class).
class TextEditorLineNumberArea : public QWidget
{
public:
    explicit TextEditorLineNumberArea(TextEditorWidget* editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TextEditorWidget* m_editor = nullptr;
};
