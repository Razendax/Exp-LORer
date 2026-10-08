#pragma once

#include <vector>

#include <QDialog>

#include "EnvironmentVariable.h"

class QTableWidget;
class QPushButton;
class EnvironmentVariablesViewModel;

// Standalone "Environment Variables" dialog (Architecture.md §14.57), opened from Tools >
// Environment Variables...: a QTableWidget (Variable | Value) plus New/Edit/Delete buttons, below
// which OK/Cancel sit. Transactional: all New/Edit/Delete clicks mutate only a local working copy
// of EnvironmentVariablesViewModel::variables(); nothing reaches the ViewModel until OK, which
// calls viewModel->save(...) once. Cancel just reject()s, leaving the ViewModel untouched.
class EnvironmentVariablesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EnvironmentVariablesDialog(EnvironmentVariablesViewModel* viewModel, QWidget* parent = nullptr);

private:
    void rebuildTable();
    void onSelectionChanged();
    void onNewClicked();
    void onEditClicked();
    void onDeleteClicked();
    void onAccept();

    // Opens EditEnvironmentVariableDialog pre-filled with `*existing` (nullptr for New), validates
    // the name is non-empty and case-insensitive-unique against every other row in m_workingCopy,
    // and on success inserts/replaces the row at `editIndex` (-1 to append). Shared by
    // onNewClicked/onEditClicked/the double-click handler.
    void editVariableAt(int rowIndex);

    EnvironmentVariablesViewModel* m_viewModel = nullptr;
    std::vector<EnvironmentVariable> m_workingCopy;

    QTableWidget* m_table = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
