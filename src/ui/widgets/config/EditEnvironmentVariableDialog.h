#pragma once

#include <QDialog>

#include "EnvironmentVariable.h"

class QLineEdit;
class QListWidget;
class QPushButton;

// Nested editor opened by EnvironmentVariablesDialog's New/Edit buttons (Architecture.md §14.57):
// a name QLineEdit plus a values QListWidget (one value per row) with a vertical New/Edit/Delete/
// Move Up/Move Down button column -- same layout idiom as ManageFavoritesDialog's button row, just
// stacked next to a list instead of under a tree. OK validates the name is non-empty (trimmed);
// the caller (EnvironmentVariablesDialog) does the case-insensitive duplicate-name check against
// sibling rows, since that check needs the rest of the working copy this dialog doesn't have.
class EditEnvironmentVariableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditEnvironmentVariableDialog(QWidget* parent = nullptr);

    void setVariable(const EnvironmentVariable& variable);
    EnvironmentVariable variable() const;

private:
    void onAccept();
    void onNewValueClicked();
    void onEditValueClicked();
    void onDeleteValueClicked();
    void onMoveValueUpClicked();
    void onMoveValueDownClicked();
    void onSelectionChanged();

    QLineEdit* m_nameEdit = nullptr;
    QListWidget* m_valuesList = nullptr;
    QPushButton* m_editValueButton = nullptr;
    QPushButton* m_deleteValueButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
};
