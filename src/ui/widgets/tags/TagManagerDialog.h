#pragma once

#include <optional>
#include <vector>

#include <QDialog>
#include <QHash>
#include <QIcon>

#include "FileNode.h"
#include "FileTagsByPath.h"
#include "Tag.h"

class FlowLayout;
class QLineEdit;
class QPushButton;
class QToolButton;
class QComboBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;
class QScrollArea;
class FileListModel;
class FileBrowserView;
class FileDecorationRules;
class TagManagerViewModel;

// Standalone "Tag Manager" dialog (Architecture.md §14.17), opened from Edit > "Tag Edit...":
// a search box, a flow of selectable tag chips, and Rename/Add/Delete buttons operating on the
// global tag list (independent of any file/folder selection, unlike TagPanelWidget). Architecture.md
// §14.89 added an optional per-chip file-count display and a files/folders results list for
// whichever chip is selected. Architecture.md §14.90 added real icons/Tags-column for that results
// list plus window size/column width/order/visibility persistence.
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    // fileDecorationRules/fileDecorationsChangeSource are forwarded straight into the results
    // list's FileListModel constructor (same dependencies MainWindow already threads into
    // SearchPanelWidget) -- this dialog needs its own model/view since it isn't backed by any tab.
    TagManagerDialog(TagManagerViewModel* viewModel, const FileDecorationRules& fileDecorationRules,
                      QObject& fileDecorationsChangeSource, QWidget* parent = nullptr);

protected:
    // QDialog's single funnel point for accept/reject/Esc/the default closeEvent-triggered reject --
    // reliably fires for this dialog's "Close" button (wired straight to QDialog::accept), unlike
    // overriding closeEvent() alone. Saves window geometry + results column widths/order/visibility
    // via QSettings before chaining to QDialog::done() (Architecture.md §14.90).
    void done(int result) override;

private:
    void rebuildTagChips(const std::vector<Tag>& tags);
    void rebuildTagTree(const std::vector<Tag>& tags);
    void onMatchingTagsChanged(const std::vector<Tag>& tags);
    void onChipClicked(Tag::Id id);
    void onTreeSelectionChanged(QTreeWidgetItem* current);
    void applySelectedTag(std::optional<Tag::Id> id);

    // Re-applies the chip/tree highlighting for `id` after rebuildTagChips()/rebuildTagTree() threw
    // away the old widgets/items -- used by onMatchingTagsChanged() to keep a still-matching
    // selection visually selected across a non-destructive rebuild (search text, show-counts, sort),
    // without touching m_selectedTagId, the Rename/Delete buttons, or the results pane, all of which
    // are already correct and shouldn't flicker.
    void highlightSelectedTag(Tag::Id id);
    void onViewModeChanged(int index);
    void onSortSettingsChanged();
    void onAddClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onOperationFailed(const QString& message);
    void onFilesForTagChanged(const std::vector<FileNode>& files);
    void onFilesForTagIconsChanged(const QHash<QString, QIcon>& icons);
    void onFilesForTagTagsChanged(const TagsByPathMap& tagsByPath);

    static constexpr const char* kWindowGeometrySettingsKey = "TagManager/WindowGeometry";
    static constexpr const char* kColumnWidthsSettingsKey = "TagManager/ColumnWidths";
    static constexpr const char* kColumnOrderSettingsKey = "TagManager/ColumnOrder";
    static constexpr const char* kColumnVisibilitySettingsKey = "TagManager/ColumnVisibility";
    static constexpr const char* kShowCountsSettingsKey = "TagManager/ShowCounts";
    static constexpr const char* kViewModeSettingsKey = "TagManager/ViewMode";
    static constexpr const char* kSortKeySettingsKey = "TagManager/SortKey";
    static constexpr const char* kSortOrderSettingsKey = "TagManager/SortOrder";

    TagManagerViewModel* m_viewModel = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    QToolButton* m_showCountsButton = nullptr;
    QComboBox* m_sortKeyCombo = nullptr;
    QComboBox* m_sortOrderCombo = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    FlowLayout* m_chipLayout = nullptr;
    QScrollArea* m_chipScrollArea = nullptr;
    QTreeWidget* m_tagTree = nullptr;
    QStackedWidget* m_tagAreaStack = nullptr;
    QSplitter* m_splitter = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    FileListModel* m_resultsModel = nullptr;
    FileBrowserView* m_resultsView = nullptr;

    std::optional<Tag::Id> m_selectedTagId;
};
