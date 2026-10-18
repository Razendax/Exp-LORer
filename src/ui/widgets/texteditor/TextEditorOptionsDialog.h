#pragma once

#include "TextEditorOptions.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

// Tools > Text Editor...'s new first stop: lets the user pick which of TextEditorOptions'
// feature flags to enable, plus a highlighting language, before TextEditorDialog is opened, so
// every combination can be exercised from the UI ahead of any other embedding site existing.
// Deliberately has no knowledge of TextEditorDialog -- it only collects and returns a
// TextEditorOptions via options(), keeping the two dialogs decoupled.
class TextEditorOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextEditorOptionsDialog(QWidget* parent = nullptr);

    TextEditorOptions options() const;

private:
    QCheckBox* m_findCheckBox = nullptr;
    QCheckBox* m_goToLineCheckBox = nullptr;
    QCheckBox* m_lineNumbersCheckBox = nullptr;
    QCheckBox* m_multicursorCheckBox = nullptr;
    QCheckBox* m_highlightSelectionOccurrencesCheckBox = nullptr;
    QCheckBox* m_readOnlyCheckBox = nullptr;
    QComboBox* m_highlightLanguageComboBox = nullptr;
};
