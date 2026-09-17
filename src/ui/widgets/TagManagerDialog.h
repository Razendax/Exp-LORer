#pragma once

#include <vector>

#include <QDialog>

#include "Tag.h"

class QLineEdit;
class QListWidget;
class QPushButton;
class TagManagerViewModel;

// Standalone "Tag Manager" dialog (Architecture.md §14.17), opened from Edit > "Tag Edit...":
// a search box, a list of matching tags, and Rename/Add/Delete buttons operating on the global
// tag list (independent of any file/folder selection, unlike TagPanelWidget).
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagManagerDialog(TagManagerViewModel* viewModel, QWidget* parent = nullptr);

private:
    void rebuildTagList(const std::vector<Tag>& tags);
    void onSelectionChanged();
    void onAddClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onOperationFailed(const QString& message);

    TagManagerViewModel* m_viewModel = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_tagList = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
