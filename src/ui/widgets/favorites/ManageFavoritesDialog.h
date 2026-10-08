#pragma once

#include <QDialog>

#include "Favorites.h"

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class FavoritesViewModel;

// Standalone "Manage Favorites" dialog (Architecture.md §14.35), opened from Favorites > "Manage
// Favorites...": a two-level QTreeWidget (category -> items, with native collapse/expand for
// requirement 5's "collapsible sections") plus Add/Rename/Move/Remove buttons, wired directly to
// FavoritesViewModel -- no separate ManageFavoritesViewModel needed since FavoritesViewModel is
// already the Qt-facing, mutation-ready owner (same posture as TagManagerDialog ->
// TagManagerViewModel).
class ManageFavoritesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManageFavoritesDialog(FavoritesViewModel* viewModel, QWidget* parent = nullptr);

private:
    void rebuildTree();
    void onSelectionChanged();
    void onAddFileClicked();
    void onAddFolderClicked();
    void onAddCategoryClicked();
    void onRenameClicked();
    void onMoveToCategoryClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onRemoveClicked();

    FavoritesViewModel* m_viewModel = nullptr;

    QTreeWidget* m_tree = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_moveToCategoryButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QPushButton* m_removeButton = nullptr;
};
