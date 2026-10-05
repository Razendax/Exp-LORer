#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "FileDecorationRule.h"
#include "Language.h"
#include "NotificationType.h"

class HighlightThemeViewModel;
class FileDecorationsViewModel;
class NotificationSettingsViewModel;
class ColumnVisibilitySettingsViewModel;
class SyntaxHighlightEngine;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class UiThemeChangeNotifier;

// File > Settings... dialog (Architecture.md §14.26): a QTreeWidget category tree on the left
// (Highlight > Languages > one entry per Language, General > UI) and a QStackedWidget on the right.
// The Highlight > Languages pages list the selected language's capture names
// (SyntaxHighlightEngine::captureNamesFor), each with a QColorDialog-backed color swatch writing
// through HighlightThemeViewModel; the General > UI page is a single checkbox controlling tab
// close-button visibility. Single "Close" button, no OK/Cancel -- edits apply and persist
// immediately, same posture as TagManagerDialog's tag edits. Structured generically (category tree
// -> stacked page) so a future settings category slots in without reshaping the dialog.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(SyntaxHighlightEngine& engine, HighlightThemeViewModel& themeViewModel,
                   FileDecorationsViewModel& fileDecorationsViewModel, NotificationSettingsViewModel& notificationSettingsViewModel,
                   ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel,
                   UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

signals:
    // General > UI "Show close button on tabs" checkbox toggled -- MainWindow propagates this to
    // every open pane (WorkspacePaneWidget::setTabCloseButtonsVisible).
    void showTabCloseButtonsChanged(bool visible);

private:
    void onCategoryChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    // Replaces the placeholder page at `languageIndex` with the real one (built via
    // buildLanguagePage, which compiles that language's tree-sitter query) the first time this
    // language is actually selected -- a no-op on every later selection. Keeps Settings' first open
    // from paying all nine languages' grammar/query setup cost up front for a QStackedWidget that
    // only ever shows one page at a time.
    void ensureLanguagePageBuilt(std::size_t languageIndex);
    QWidget* buildLanguagePage(Language language);
    QWidget* buildGeneralUiPage();
    void openColorPicker(Language language, const std::string& captureName, QPushButton* swatchButton);
    void refreshSwatchColors();

    // General -> Colors page (Architecture.md §14.29): per-file/folder font & color customization
    // by name pattern, plus two built-in non-removable rows (Hidden Files/Hidden Folders) always
    // pinned at the top of the table. m_decorationRules is a local editable copy of
    // m_fileDecorationsViewModel.rules(); m_hiddenFilesRule/m_hiddenFoldersRule likewise mirror
    // hiddenFilesRule()/hiddenFoldersRule() -- every mutation (add/edit/reorder/remove for pattern
    // rules, edit for the two hidden rows) updates the local copy and immediately calls the
    // matching setRules()/setHiddenFilesRule()/setHiddenFoldersRule() to persist + broadcast,
    // matching every other Settings control's immediate-apply posture. Table rows 0/1 are always
    // the hidden rows; general rules render at row `2 + i` for vector index `i`.
    QWidget* buildFileDecorationsPage();
    void refreshDecorationRows();
    void addDecorationRule();
    void editDecorationRule(int row);
    void editHiddenRule(bool isDirectory);
    void moveDecorationRule(int row, int delta);
    void removeDecorationRule(int row);
    void commitDecorationRules();

    // General -> Notifications page: a fixed 3-row table (Type / Enabled / Timeout), one row per
    // NotificationType, built once from NotificationSettingsViewModel::settingsFor(type) -- each
    // control writes straight through setSettingsFor(type, enabled, timeoutMs) on change, no
    // Add/Remove since the 3 rows never change.
    QWidget* buildNotificationsPage();

    // General -> Columns page (Architecture.md §14.x): a "Default visibility for new tab" group
    // box with one row per non-Name Details-view column, each with a single checkbox writing
    // straight through to setDefaultVisibility(column, checked) -- same wiring shape as
    // buildNotificationsPage()'s enabled-checkbox column, minus the timeout spin box.
    QWidget* buildColumnsPage();

    static constexpr int kHiddenRowCount = 2;

    SyntaxHighlightEngine& m_engine;
    HighlightThemeViewModel& m_themeViewModel;
    FileDecorationsViewModel& m_fileDecorationsViewModel;
    NotificationSettingsViewModel& m_notificationSettingsViewModel;
    ColumnVisibilitySettingsViewModel& m_columnVisibilitySettingsViewModel;
    UiThemeChangeNotifier& m_uiThemeChangeNotifier;
    QTableWidget* m_decorationsTable = nullptr;
    std::vector<FileDecorationRule> m_decorationRules;
    FileDecorationRule m_hiddenFilesRule;
    FileDecorationRule m_hiddenFoldersRule;

    QTreeWidget* m_categoryTree = nullptr;
    QStackedWidget* m_pageStack = nullptr;
    QCheckBox* m_showTabCloseButtonsCheckBox = nullptr;
    QComboBox* m_themeComboBox = nullptr;
    // Parallel to kAllLanguages -- null until ensureLanguagePageBuilt() replaces that index's
    // placeholder with the real page.
    std::vector<QWidget*> m_languagePages;

    struct TokenRow
    {
        Language language;
        std::string captureName;
        QPushButton* swatchButton = nullptr;
    };
    std::vector<TokenRow> m_tokenRows;
};
