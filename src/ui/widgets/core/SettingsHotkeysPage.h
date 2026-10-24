#pragma once

#include <vector>

#include <QKeySequence>
#include <QWidget>

#include "HotkeyAction.h"
#include "HotkeyCatalogEntry.h"

class QLineEdit;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class HotkeyManager;
class HotkeyEditWidget;

// General > Hotkeys settings page (spec §4): one row per HotkeyCatalog::entries(), grouped under
// collapsible category headers, with a filter box above the table, per-row HotkeyEditWidget +
// Clear/Reset buttons and an inline conflict warning, plus a bottom "Restore All Defaults" button.
// Built once in full in Milestone 1 (populated with only Milestone 1's 10 rows); later milestones
// only add catalog rows -- no structural change needed here.
class SettingsHotkeysPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsHotkeysPage(HotkeyManager& hotkeyManager, QWidget* parent = nullptr);

private:
    struct Row
    {
        HotkeyAction action = HotkeyAction::Copy;
        QTreeWidgetItem* item = nullptr;
        HotkeyEditWidget* editWidget = nullptr;
        QLabel* conflictLabel = nullptr;
    };

    void rebuildRows();
    void refreshRow(Row& row);
    void refreshAllRows();
    void applyFilter(const QString& text);
    void onChordAccepted(Row& row, const QKeySequence& chord);
    void onClearClicked(Row& row);
    void onResetClicked(Row& row);
    void onRestoreAllDefaultsClicked();

    static QString categoryDisplayName(HotkeyCatalogEntry::Category category);

    HotkeyManager& m_hotkeyManager;
    QLineEdit* m_filterEdit = nullptr;
    QTreeWidget* m_tree = nullptr;

    // Reserved up front to the catalog's exact size before any row is pushed (rebuildRows()) --
    // every Clear/Reset/HotkeyEditWidget connection below captures a reference to its own element,
    // so this vector must never reallocate once populated.
    std::vector<Row> m_rows;
};
