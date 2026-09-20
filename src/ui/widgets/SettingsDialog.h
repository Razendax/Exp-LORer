#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "Language.h"

class HighlightThemeViewModel;
class SyntaxHighlightEngine;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// File > Settings... dialog (Architecture.md §14.26): a QTreeWidget category tree on the left
// (Highlight > Languages > one entry per Language) and a QStackedWidget on the right listing the
// selected language's capture names (SyntaxHighlightEngine::captureNamesFor), each with a
// QColorDialog-backed color swatch writing through HighlightThemeViewModel. Single "Close" button,
// no OK/Cancel -- edits apply and persist immediately, same posture as TagManagerDialog's tag edits.
// Structured generically (category tree -> stacked page) so a future non-highlighting settings
// category slots in without reshaping the dialog.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(SyntaxHighlightEngine& engine, HighlightThemeViewModel& themeViewModel, QWidget* parent = nullptr);

private:
    void onCategoryChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    // Replaces the placeholder page at `languageIndex` with the real one (built via
    // buildLanguagePage, which compiles that language's tree-sitter query) the first time this
    // language is actually selected -- a no-op on every later selection. Keeps Settings' first open
    // from paying all nine languages' grammar/query setup cost up front for a QStackedWidget that
    // only ever shows one page at a time.
    void ensureLanguagePageBuilt(std::size_t languageIndex);
    QWidget* buildLanguagePage(Language language);
    void openColorPicker(Language language, const std::string& captureName, QPushButton* swatchButton);
    void refreshSwatchColors();

    SyntaxHighlightEngine& m_engine;
    HighlightThemeViewModel& m_themeViewModel;

    QTreeWidget* m_categoryTree = nullptr;
    QStackedWidget* m_pageStack = nullptr;
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
