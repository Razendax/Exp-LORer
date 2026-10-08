#pragma once

#include <optional>
#include <vector>

#include <QDialog>

#include "Tag.h"

class FlowLayout;
class QLineEdit;
class QPushButton;
class TagManagerViewModel;

// Standalone "Tag Manager" dialog (Architecture.md §14.17), opened from Edit > "Tag Edit...":
// a search box, a flow of selectable tag chips, and Rename/Add/Delete buttons operating on the
// global tag list (independent of any file/folder selection, unlike TagPanelWidget).
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagManagerDialog(TagManagerViewModel* viewModel, QWidget* parent = nullptr);

private:
    void rebuildTagChips(const std::vector<Tag>& tags);
    void onChipClicked(Tag::Id id);
    void onAddClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onOperationFailed(const QString& message);

    TagManagerViewModel* m_viewModel = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    FlowLayout* m_chipLayout = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;

    std::optional<Tag::Id> m_selectedTagId;
};
