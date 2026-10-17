#pragma once

#include <QDialog>

class TextEditorPanel;

// Tools > Text Editor... (ToDo.md "Core editing" section): a standalone, non-file-backed dialog
// hosting TextEditorPanel. Deliberately bare for now -- no open/save/encoding handling yet, just
// a scratch pad to build the editor widget up from.
class TextEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextEditorDialog(QWidget* parent = nullptr);

private:
    TextEditorPanel* m_panel = nullptr;
};
