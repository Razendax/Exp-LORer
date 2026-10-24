#include "SettingsHotkeysPage.h"

#include <algorithm>
#include <array>
#include <optional>

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "HotkeyEditWidget.h"
#include "HotkeyManager.h"

namespace
{
    constexpr int kActionColumn = 0;
    constexpr int kShortcutColumn = 1;
    constexpr int kButtonsColumn = 2;
    constexpr int kConflictColumn = 3;

    // Category header order matches spec §3's own subsection order, independent of whatever order
    // HotkeyCatalog::entries() happens to list rows in.
    constexpr std::array<HotkeyCatalogEntry::Category, 6> kCategoryOrder = {
        HotkeyCatalogEntry::Category::Navigation,     HotkeyCatalogEntry::Category::Tabs,
        HotkeyCatalogEntry::Category::PanesAndLayout, HotkeyCatalogEntry::Category::Panels,
        HotkeyCatalogEntry::Category::SearchAndTools, HotkeyCatalogEntry::Category::FileOperations,
    };
}

QString SettingsHotkeysPage::categoryDisplayName(HotkeyCatalogEntry::Category category)
{
    switch (category)
    {
        case HotkeyCatalogEntry::Category::Navigation:
            return tr("Navigation");
        case HotkeyCatalogEntry::Category::Tabs:
            return tr("Tabs");
        case HotkeyCatalogEntry::Category::PanesAndLayout:
            return tr("Panes & Layout");
        case HotkeyCatalogEntry::Category::Panels:
            return tr("Panels");
        case HotkeyCatalogEntry::Category::SearchAndTools:
            return tr("Search & Tools");
        case HotkeyCatalogEntry::Category::FileOperations:
            return tr("File Operations & Selection");
    }
    return QString();
}

SettingsHotkeysPage::SettingsHotkeysPage(HotkeyManager& hotkeyManager, QWidget* parent)
    : QWidget(parent)
    , m_hotkeyManager(hotkeyManager)
{
    auto* layout = new QVBoxLayout(this);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("settingsHotkeysFilterEdit"));
    m_filterEdit->setPlaceholderText(tr("Filter actions..."));
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("settingsHotkeysTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Action"), tr("Shortcut"), QString(), tr("Conflict") });
    m_tree->header()->setSectionResizeMode(kActionColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kShortcutColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kButtonsColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kConflictColumn, QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);

    auto* restoreAllButton = new QPushButton(tr("Restore All Defaults"), this);
    restoreAllButton->setObjectName(QStringLiteral("settingsHotkeysRestoreAllButton"));
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(restoreAllButton);
    layout->addLayout(buttonRow);

    rebuildRows();

    connect(m_filterEdit, &QLineEdit::textChanged, this, &SettingsHotkeysPage::applyFilter);
    connect(restoreAllButton, &QPushButton::clicked, this, &SettingsHotkeysPage::onRestoreAllDefaultsClicked);
    connect(&m_hotkeyManager, &HotkeyManager::bindingsChanged, this, &SettingsHotkeysPage::refreshAllRows);
}

void SettingsHotkeysPage::rebuildRows()
{
    m_tree->clear();
    m_rows.clear();
    // See the header comment on m_rows: every row below hands out a reference into this vector to
    // a Qt connection, so it must never reallocate once rows start being pushed -- reserving the
    // catalog's exact size up front guarantees that.
    m_rows.reserve(m_hotkeyManager.catalog().size());

    for (HotkeyCatalogEntry::Category category : kCategoryOrder)
    {
        std::vector<const HotkeyCatalogEntry*> entriesInCategory;
        for (const HotkeyCatalogEntry& entry : m_hotkeyManager.catalog())
        {
            if (entry.category == category)
            {
                entriesInCategory.push_back(&entry);
            }
        }
        if (entriesInCategory.empty())
        {
            continue;
        }

        auto* categoryItem = new QTreeWidgetItem(m_tree, QStringList{ categoryDisplayName(category) });
        categoryItem->setFlags(Qt::ItemIsEnabled);
        QFont boldFont = categoryItem->font(0);
        boldFont.setBold(true);
        categoryItem->setFont(0, boldFont);

        for (const HotkeyCatalogEntry* entry : entriesInCategory)
        {
            m_rows.push_back(Row{});
            Row& row = m_rows.back();
            row.action = entry->action;
            row.item = new QTreeWidgetItem(categoryItem, QStringList{ entry->displayName });

            auto* editWidget = new HotkeyEditWidget(m_tree);
            editWidget->setAllowsBareRebind(entry->allowsBareRebind);
            row.editWidget = editWidget;
            m_tree->setItemWidget(row.item, kShortcutColumn, editWidget);
            connect(editWidget, &HotkeyEditWidget::chordAccepted, this,
                    [this, &row](const QKeySequence& chord) { onChordAccepted(row, chord); });

            auto* buttonsWidget = new QWidget(m_tree);
            auto* buttonsLayout = new QHBoxLayout(buttonsWidget);
            buttonsLayout->setContentsMargins(0, 0, 0, 0);
            auto* clearButton = new QToolButton(buttonsWidget);
            clearButton->setObjectName(QStringLiteral("settingsHotkeyClearButton"));
            clearButton->setText(tr("Clear"));
            auto* resetButton = new QToolButton(buttonsWidget);
            resetButton->setObjectName(QStringLiteral("settingsHotkeyResetButton"));
            resetButton->setText(tr("Reset"));
            buttonsLayout->addWidget(clearButton);
            buttonsLayout->addWidget(resetButton);
            m_tree->setItemWidget(row.item, kButtonsColumn, buttonsWidget);
            connect(clearButton, &QToolButton::clicked, this, [this, &row]() { onClearClicked(row); });
            connect(resetButton, &QToolButton::clicked, this, [this, &row]() { onResetClicked(row); });

            row.conflictLabel = new QLabel(m_tree);
            row.conflictLabel->setObjectName(QStringLiteral("settingsHotkeyConflictLabel"));
            row.conflictLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
            m_tree->setItemWidget(row.item, kConflictColumn, row.conflictLabel);

            refreshRow(row);
        }
    }

    m_tree->expandAll();
}

void SettingsHotkeysPage::refreshRow(Row& row)
{
    const QKeySequence chord = m_hotkeyManager.chordFor(row.action);
    row.editWidget->setChord(chord);

    const std::optional<HotkeyAction> conflict = m_hotkeyManager.conflictingAction(row.action, chord);
    if (!conflict)
    {
        row.conflictLabel->clear();
        return;
    }

    const auto& catalog = m_hotkeyManager.catalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                  [conflict](const HotkeyCatalogEntry& entry) { return entry.action == *conflict; });
    row.conflictLabel->setText(it != catalog.end() ? tr("Conflicts with \"%1\"").arg(it->displayName) : QString());
}

void SettingsHotkeysPage::refreshAllRows()
{
    for (Row& row : m_rows)
    {
        refreshRow(row);
    }
}

void SettingsHotkeysPage::applyFilter(const QString& text)
{
    for (Row& row : m_rows)
    {
        const bool matches = text.isEmpty() || row.item->text(kActionColumn).contains(text, Qt::CaseInsensitive);
        row.item->setHidden(!matches);
    }

    // Hides a category header once every one of its child rows is filtered out -- same convenience
    // as other filter boxes in the app.
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* categoryItem = m_tree->topLevelItem(i);
        bool anyVisible = false;
        for (int c = 0; c < categoryItem->childCount(); ++c)
        {
            if (!categoryItem->child(c)->isHidden())
            {
                anyVisible = true;
                break;
            }
        }
        categoryItem->setHidden(!anyVisible);
    }
}

void SettingsHotkeysPage::onChordAccepted(Row& row, const QKeySequence& chord)
{
    m_hotkeyManager.setBinding(row.action, chord);
    // A successful setBinding() emits bindingsChanged(), already connected (constructor) to
    // refreshAllRows() -- that repaints this row's own display plus any newly-conflicting other
    // row's warning label. A rejected bare-key rebind emits nothing, leaving every row exactly as
    // it was, which is already correct either way.
}

void SettingsHotkeysPage::onClearClicked(Row& row)
{
    m_hotkeyManager.clearBinding(row.action);
}

void SettingsHotkeysPage::onResetClicked(Row& row)
{
    m_hotkeyManager.resetToDefault(row.action);
}

void SettingsHotkeysPage::onRestoreAllDefaultsClicked()
{
    const auto answer = QMessageBox::question(this, tr("Restore All Defaults"),
                                               tr("Reset every hotkey to its default shortcut? This can't be undone."),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
    {
        m_hotkeyManager.resetAll();
    }
}
