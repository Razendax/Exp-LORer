#include "TextEditorLineNumberArea.h"

#include "TextEditorWidget.h"

TextEditorLineNumberArea::TextEditorLineNumberArea(TextEditorWidget* editor)
    : QWidget(editor)
    , m_editor(editor)
{
}

QSize TextEditorLineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void TextEditorLineNumberArea::paintEvent(QPaintEvent* event)
{
    m_editor->lineNumberAreaPaintEvent(event);
}
