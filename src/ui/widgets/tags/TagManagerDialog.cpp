#include "TagManagerDialog.h"

#include <algorithm>
#include <array>
#include <functional>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantList>

#include "FileBrowserView.h"
#include "FileListModel.h"
#include "FlowLayout.h"
#include "TagChipWidget.h"
#include "TagManagerViewModel.h"

namespace
{
constexpr int kTagIdRole = Qt::UserRole;
constexpr int kIsVirtualRole = Qt::UserRole + 1;
} // namespace

TagManagerDialog::TagManagerDialog(TagManagerViewModel* viewModel, const FileDecorationRules& fileDecorationRules,
                                    QObject& fileDecorationsChangeSource, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setObjectName(QStringLiteral("tagManagerDialog"));
    setWindowTitle(tr("Tag Manager"));
    resize(800, 600);

    auto* mainLayout = new QVBoxLayout(this);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("tagManagerSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("Search tags..."));
    searchRow->addWidget(m_searchEdit);

    m_showCountsButton = new QToolButton(this);
    m_showCountsButton->setObjectName(QStringLiteral("tagManagerShowCountsButton"));
    m_showCountsButton->setText(QStringLiteral("#"));
    m_showCountsButton->setToolTip(tr("Show file count per tag"));
    m_showCountsButton->setCheckable(true);
    searchRow->addWidget(m_showCountsButton);

    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->setObjectName(QStringLiteral("tagManagerViewModeCombo"));
    m_viewModeCombo->addItem(tr("List"));
    m_viewModeCombo->addItem(tr("Tree"));
    searchRow->addWidget(m_viewModeCombo);
    mainLayout->addLayout(searchRow);

    auto* chipContent = new QWidget;
    m_chipLayout = new FlowLayout(chipContent);

    m_chipScrollArea = new QScrollArea;
    m_chipScrollArea->setWidget(chipContent);
    m_chipScrollArea->setWidgetResizable(true);
    m_chipScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_tagTree = new QTreeWidget;
    m_tagTree->setObjectName(QStringLiteral("tagManagerTagTree"));
    m_tagTree->setHeaderHidden(true);
    m_tagTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tagTree->setSortingEnabled(true);
    m_tagTree->sortByColumn(0, Qt::AscendingOrder);

    m_tagAreaStack = new QStackedWidget;
    m_tagAreaStack->addWidget(m_chipScrollArea);
    m_tagAreaStack->addWidget(m_tagTree);

    m_resultsModel = new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this);
    m_resultsView = new FileBrowserView(m_resultsModel, this, FileBrowserView::DisplayMode::AdvancedSearchResults);
    m_resultsView->setObjectName(QStringLiteral("tagManagerResultsView"));

    bool storedShowCounts = false;
    int storedViewMode = 0;

    {
        QSettings settings;
        const QByteArray geometry = settings.value(QLatin1String(kWindowGeometrySettingsKey)).toByteArray();
        if (!geometry.isEmpty())
        {
            restoreGeometry(geometry);
        }

        storedShowCounts = settings.value(QLatin1String(kShowCountsSettingsKey), false).toBool();
        storedViewMode = settings.value(QLatin1String(kViewModeSettingsKey), 0).toInt();

        // Tolerant of a missing/short/empty list (e.g. first-ever open, or FileListModel::ColumnCount
        // having changed since) -- any column left unread keeps its default-constructed 0 ("unset"),
        // which FileBrowserView::setColumnWidths() already treats as "use the default" per its <= 0
        // sentinel convention. Safe to call unconditionally right after construction, same posture as
        // that convention's other callers.
        std::array<int, FileListModel::ColumnCount> columnWidths{};
        const QVariantList storedColumnWidths = settings.value(QLatin1String(kColumnWidthsSettingsKey)).toList();
        for (int i = 0; i < storedColumnWidths.size() && i < static_cast<int>(columnWidths.size()); ++i)
        {
            columnWidths[static_cast<std::size_t>(i)] = storedColumnWidths.at(i).toInt();
        }
        m_resultsView->setColumnWidths(columnWidths);

        // Same tolerant-read posture: a missing/short list leaves trailing entries at their
        // default-constructed 0, and a zero-filled array is not a valid permutation, so
        // setColumnOrder() already no-ops on it (first-ever open, or a changed ColumnCount since).
        std::array<int, FileListModel::ColumnCount> columnOrder{};
        const QVariantList storedColumnOrder = settings.value(QLatin1String(kColumnOrderSettingsKey)).toList();
        for (int i = 0; i < storedColumnOrder.size() && i < static_cast<int>(columnOrder.size()); ++i)
        {
            columnOrder[static_cast<std::size_t>(i)] = storedColumnOrder.at(i).toInt();
        }
        m_resultsView->setColumnOrder(columnOrder);

        // No stored value (first-ever open) leaves every column visible, matching
        // FileBrowserView's own default; NameColumn's entry is read but ignored --
        // setColumnVisible() no-ops on it regardless.
        const QVariantList storedColumnVisibility = settings.value(QLatin1String(kColumnVisibilitySettingsKey)).toList();
        for (int i = 0; i < storedColumnVisibility.size() && i < FileListModel::ColumnCount; ++i)
        {
            m_resultsView->setColumnVisible(static_cast<FileListModel::Column>(i), storedColumnVisibility.at(i).toBool());
        }
    }

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_tagAreaStack);
    m_splitter->addWidget(m_resultsView);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_splitter, 1);

    auto* buttonLayout = new QHBoxLayout();
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_renameButton->setObjectName(QStringLiteral("tagManagerRenameButton"));
    m_addButton = new QPushButton(tr("Add"), this);
    m_addButton->setObjectName(QStringLiteral("tagManagerAddButton"));
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setObjectName(QStringLiteral("tagManagerDeleteButton"));
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("tagManagerCloseButton"));
    m_renameButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_searchEdit, &QLineEdit::textChanged, m_viewModel, &TagManagerViewModel::setSearchQuery);
    connect(m_showCountsButton, &QToolButton::toggled, m_viewModel, &TagManagerViewModel::setShowCounts);
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TagManagerDialog::onViewModeChanged);
    connect(m_addButton, &QPushButton::clicked, this, &TagManagerDialog::onAddClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &TagManagerDialog::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &TagManagerDialog::onDeleteClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_tagTree, &QTreeWidget::currentItemChanged, this, &TagManagerDialog::onTreeSelectionChanged);

    connect(m_viewModel, &TagManagerViewModel::matchingTagsChanged, this, &TagManagerDialog::onMatchingTagsChanged);
    connect(m_viewModel, &TagManagerViewModel::operationFailed, this, &TagManagerDialog::onOperationFailed);
    connect(m_viewModel, &TagManagerViewModel::filesForTagChanged, this, &TagManagerDialog::onFilesForTagChanged);
    connect(m_viewModel, &TagManagerViewModel::filesForTagIconsChanged, this, &TagManagerDialog::onFilesForTagIconsChanged);
    connect(m_viewModel, &TagManagerViewModel::filesForTagTagsChanged, this, &TagManagerDialog::onFilesForTagTagsChanged);

    m_showCountsButton->setChecked(storedShowCounts);
    m_viewModeCombo->setCurrentIndex(storedViewMode);

    onMatchingTagsChanged(m_viewModel->matchingTags());
}

void TagManagerDialog::rebuildTagChips(const std::vector<Tag>& tags)
{
    while (m_chipLayout->count() > 0)
    {
        QLayoutItem* item = m_chipLayout->takeAt(0);
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }

    for (const Tag& tag : tags)
    {
        std::optional<int> count;
        std::optional<int> totalCount;
        if (m_viewModel->showCounts())
        {
            count = m_viewModel->countForTag(tag.id());

            // Flat list has no tree structure to walk, so "children" is recomputed here the same
            // way the tree builder derives it: any other currently-matching tag whose name is
            // nested under this one via the §14.9/§14.88 string-prefix `TagHierarchy` convention.
            const QString prefix = QString::fromStdString(tag.name()) + QLatin1Char('/');
            int childrenSum = 0;
            bool hasChildren = false;
            for (const Tag& other : tags)
            {
                if (other.id() != tag.id() && QString::fromStdString(other.name()).startsWith(prefix))
                {
                    hasChildren = true;
                    childrenSum += m_viewModel->countForTag(other.id());
                }
            }

            if (hasChildren)
            {
                totalCount = *count + childrenSum;
            }
        }

        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Selectable, count, totalCount);
        connect(chip, &TagChipWidget::clicked, this, &TagManagerDialog::onChipClicked);
        m_chipLayout->addWidget(chip);
    }
}

void TagManagerDialog::rebuildTagTree(const std::vector<Tag>& tags)
{
    m_tagTree->clear();

    QHash<QString, QTreeWidgetItem*> pathToItem;
    std::function<QTreeWidgetItem*(const QString&)> ensurePath = [&](const QString& fullPath) -> QTreeWidgetItem* {
        if (auto it = pathToItem.find(fullPath); it != pathToItem.end())
        {
            return it.value();
        }
        const int lastSlash = fullPath.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = lastSlash < 0 ? nullptr : ensurePath(fullPath.left(lastSlash));
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tagTree);
        item->setText(0, fullPath.mid(lastSlash + 1));
        item->setData(0, kIsVirtualRole, true);
        pathToItem.insert(fullPath, item);
        return item;
    };

    for (const Tag& tag : tags)
    {
        QTreeWidgetItem* item = ensurePath(QString::fromStdString(tag.name()));
        item->setData(0, kTagIdRole, static_cast<qlonglong>(tag.id()));
        item->setData(0, kIsVirtualRole, false);
    }

    if (m_viewModel->showCounts())
    {
        // Post-order: nested tags are a string-prefix display convention (Architecture.md
        // §14.9/§14.89), not a DB relation, so a parent's "total" is just the sum of its own count
        // (if it's a real tag, not just a path-grouping node) plus every descendant tag's own count.
        // A path-grouping node (e.g. "Project" when only "Project/Foo" is an actual tag) always has
        // at least one child, so it only ever shows a total, never an "own" count.
        std::function<int(QTreeWidgetItem*)> applyCounts = [&](QTreeWidgetItem* item) -> int {
            int childrenTotal = 0;
            for (int i = 0; i < item->childCount(); ++i)
            {
                childrenTotal += applyCounts(item->child(i));
            }

            const bool isVirtual = item->data(0, kIsVirtualRole).toBool();
            const int ownCount =
                isVirtual ? 0 : m_viewModel->countForTag(static_cast<Tag::Id>(item->data(0, kTagIdRole).toLongLong()));
            const int subtreeTotal = ownCount + childrenTotal;

            const QString name = item->text(0);
            if (item->childCount() == 0)
            {
                item->setText(0, name + QStringLiteral("  (%1)").arg(ownCount));
            }
            else if (isVirtual)
            {
                item->setText(0, name + tr("  (%1 total)").arg(subtreeTotal));
            }
            else
            {
                item->setText(0, name + tr("  (%1, %2 total)").arg(ownCount).arg(subtreeTotal));
            }

            return subtreeTotal;
        };

        for (int i = 0; i < m_tagTree->topLevelItemCount(); ++i)
        {
            applyCounts(m_tagTree->topLevelItem(i));
        }
    }

    m_tagTree->expandAll();
}

void TagManagerDialog::onMatchingTagsChanged(const std::vector<Tag>& tags)
{
    rebuildTagChips(tags);
    rebuildTagTree(tags);
    applySelectedTag(std::nullopt);
}

void TagManagerDialog::onChipClicked(Tag::Id id)
{
    for (int i = 0; i < m_chipLayout->count(); ++i)
    {
        if (auto* chip = qobject_cast<TagChipWidget*>(m_chipLayout->itemAt(i)->widget()))
        {
            chip->setSelected(chip->tag().id() == id);
        }
    }

    applySelectedTag(id);
}

void TagManagerDialog::onTreeSelectionChanged(QTreeWidgetItem* current)
{
    if (!current || current->data(0, kIsVirtualRole).toBool())
    {
        applySelectedTag(std::nullopt);
        return;
    }

    applySelectedTag(static_cast<Tag::Id>(current->data(0, kTagIdRole).toLongLong()));
}

void TagManagerDialog::applySelectedTag(std::optional<Tag::Id> id)
{
    m_selectedTagId = id;
    m_renameButton->setEnabled(id.has_value());
    m_deleteButton->setEnabled(id.has_value());
    m_viewModel->setSelectedTag(id);
}

void TagManagerDialog::onViewModeChanged(int index)
{
    const bool treeMode = (index == 1);
    m_tagAreaStack->setCurrentWidget(treeMode ? static_cast<QWidget*>(m_tagTree)
                                               : static_cast<QWidget*>(m_chipScrollArea));
    m_splitter->setOrientation(treeMode ? Qt::Horizontal : Qt::Vertical);
    applySelectedTag(std::nullopt);
    if (treeMode)
    {
        m_splitter->setSizes({250, 550});
    }
}

void TagManagerDialog::onAddClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add Tag"), tr("Tag name:"), QLineEdit::Normal,
                                                m_searchEdit->text().trimmed(), &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }

    m_viewModel->addTag(name.trimmed());
}

void TagManagerDialog::onRenameClicked()
{
    if (!m_selectedTagId.has_value())
    {
        return;
    }

    const std::vector<Tag>& tags = m_viewModel->matchingTags();
    const auto it = std::find_if(tags.begin(), tags.end(),
                                  [id = *m_selectedTagId](const Tag& tag) { return tag.id() == id; });
    if (it == tags.end())
    {
        return;
    }

    const QString currentName = QString::fromStdString(it->name());

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Tag"), tr("New name:"), QLineEdit::Normal,
                                                    currentName, &ok);
    const QString trimmedNewName = newName.trimmed();
    if (!ok || trimmedNewName.isEmpty() || trimmedNewName == currentName)
    {
        return;
    }

    const std::vector<Tag> allTags = m_viewModel->allTags();
    const auto collision = std::find_if(allTags.begin(), allTags.end(), [&](const Tag& tag) {
        return tag.id() != *m_selectedTagId &&
               QString::fromStdString(tag.name()).compare(trimmedNewName, Qt::CaseInsensitive) == 0;
    });

    if (collision != allTags.end())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Merge Tags"),
            tr("A tag named \"%1\" already exists. Merge \"%2\" into it? All files/folders tagged \"%2\" will be "
               "retagged as \"%1\", and \"%2\" will be removed.")
                .arg(QString::fromStdString(collision->name()), currentName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            return;
        }

        // On success, matchingTagsChanged fires (the source tag is gone) and rebuildTagChips's
        // own reset clears m_selectedTagId/disables Rename/Delete, same as onDeleteClicked's
        // success path; on failure (operationFailed only), nothing here needs to change since the
        // source tag still exists, selected, unmerged.
        m_viewModel->mergeTags(*m_selectedTagId, collision->id());
        return;
    }

    m_viewModel->renameTag(*m_selectedTagId, trimmedNewName);
}

void TagManagerDialog::onDeleteClicked()
{
    if (!m_selectedTagId.has_value())
    {
        return;
    }

    const std::vector<Tag>& tags = m_viewModel->matchingTags();
    const auto it = std::find_if(tags.begin(), tags.end(),
                                  [id = *m_selectedTagId](const Tag& tag) { return tag.id() == id; });
    if (it == tags.end())
    {
        return;
    }

    const QString name = QString::fromStdString(it->name());

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Delete Tag"),
                               tr("Delete tag \"%1\"? It will be removed from every file and folder it is applied to.")
                                   .arg(name));
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    m_viewModel->deleteTag(*m_selectedTagId);
}

void TagManagerDialog::onOperationFailed(const QString& message)
{
    QMessageBox::warning(this, tr("Tag Manager"), message);
}

void TagManagerDialog::onFilesForTagChanged(const std::vector<FileNode>& files)
{
    m_resultsModel->setEntries({}, files);
}

void TagManagerDialog::onFilesForTagIconsChanged(const QHash<QString, QIcon>& icons)
{
    m_resultsModel->setIcons(icons);
}

void TagManagerDialog::onFilesForTagTagsChanged(const TagsByPathMap& tagsByPath)
{
    m_resultsModel->setFileTags(tagsByPath);
}

void TagManagerDialog::done(int result)
{
    QSettings settings;
    settings.setValue(QLatin1String(kWindowGeometrySettingsKey), saveGeometry());
    settings.setValue(QLatin1String(kShowCountsSettingsKey), m_showCountsButton->isChecked());
    settings.setValue(QLatin1String(kViewModeSettingsKey), m_viewModeCombo->currentIndex());

    QVariantList columnWidths;
    for (int width : m_resultsView->columnWidths())
    {
        columnWidths.append(width);
    }
    settings.setValue(QLatin1String(kColumnWidthsSettingsKey), columnWidths);

    QVariantList columnOrder;
    for (int logicalColumn : m_resultsView->columnOrder())
    {
        columnOrder.append(logicalColumn);
    }
    settings.setValue(QLatin1String(kColumnOrderSettingsKey), columnOrder);

    QVariantList columnVisibility;
    for (int column = 0; column < FileListModel::ColumnCount; ++column)
    {
        columnVisibility.append(m_resultsView->isColumnVisible(static_cast<FileListModel::Column>(column)));
    }
    settings.setValue(QLatin1String(kColumnVisibilitySettingsKey), columnVisibility);

    QDialog::done(result);
}
