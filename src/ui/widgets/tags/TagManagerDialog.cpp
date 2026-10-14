#include "TagManagerDialog.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>

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

    m_sortKeyCombo = new QComboBox(this);
    m_sortKeyCombo->setObjectName(QStringLiteral("tagManagerSortKeyCombo"));
    m_sortKeyCombo->addItem(tr("Alphabetical"));
    m_sortKeyCombo->addItem(tr("Count"));
    searchRow->addWidget(m_sortKeyCombo);

    m_sortOrderCombo = new QComboBox(this);
    m_sortOrderCombo->setObjectName(QStringLiteral("tagManagerSortOrderCombo"));
    m_sortOrderCombo->addItem(tr("Ascending"));
    m_sortOrderCombo->addItem(tr("Descending"));
    searchRow->addWidget(m_sortOrderCombo);

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

    m_tagAreaStack = new QStackedWidget;
    m_tagAreaStack->addWidget(m_chipScrollArea);
    m_tagAreaStack->addWidget(m_tagTree);

    m_resultsModel = new FileListModel(fileDecorationRules, fileDecorationsChangeSource, this);
    m_resultsView = new FileBrowserView(m_resultsModel, this, FileBrowserView::DisplayMode::AdvancedSearchResults);
    m_resultsView->setObjectName(QStringLiteral("tagManagerResultsView"));

    bool storedShowCounts = false;
    int storedViewMode = 0;
    int storedSortKey = 0;
    int storedSortOrder = 0;

    {
        QSettings settings;
        const QByteArray geometry = settings.value(QLatin1String(kWindowGeometrySettingsKey)).toByteArray();
        if (!geometry.isEmpty())
        {
            restoreGeometry(geometry);
        }

        storedShowCounts = settings.value(QLatin1String(kShowCountsSettingsKey), false).toBool();
        storedViewMode = settings.value(QLatin1String(kViewModeSettingsKey), 0).toInt();
        storedSortKey = settings.value(QLatin1String(kSortKeySettingsKey), 0).toInt();
        storedSortOrder = settings.value(QLatin1String(kSortOrderSettingsKey), 0).toInt();

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
    connect(m_sortKeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TagManagerDialog::onSortSettingsChanged);
    connect(m_sortOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TagManagerDialog::onSortSettingsChanged);
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
    m_sortKeyCombo->setCurrentIndex(storedSortKey);
    m_sortOrderCombo->setCurrentIndex(storedSortOrder);
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

    // Architecture.md §14.92: own/total counts are computed once per tag up front -- gated on
    // countsNeeded() rather than just showCounts() -- so they can double as the Count sort key
    // even when the count display toggle is off.
    struct ChipInfo
    {
        Tag tag;
        int ownCount = 0;
        bool hasChildren = false;
        int totalCount = 0;
    };

    std::vector<ChipInfo> chipInfos;
    chipInfos.reserve(tags.size());
    const bool needCounts = m_viewModel->countsNeeded();
    for (const Tag& tag : tags)
    {
        ChipInfo info{tag};
        if (needCounts)
        {
            info.ownCount = m_viewModel->countForTag(tag.id());

            // Flat list has no tree structure to walk, so "children" is recomputed here the same
            // way the tree builder derives it: any other currently-matching tag whose name is
            // nested under this one via the §14.9/§14.88 string-prefix `TagHierarchy` convention.
            const QString prefix = QString::fromStdString(tag.name()) + QLatin1Char('/');
            int childrenSum = 0;
            for (const Tag& other : tags)
            {
                if (other.id() != tag.id() && QString::fromStdString(other.name()).startsWith(prefix))
                {
                    info.hasChildren = true;
                    childrenSum += m_viewModel->countForTag(other.id());
                }
            }

            info.totalCount = info.ownCount + childrenSum;
        }
        chipInfos.push_back(std::move(info));
    }

    const TagManagerViewModel::SortKey sortKey = m_viewModel->sortKey();
    const TagManagerViewModel::SortOrder sortOrder = m_viewModel->sortOrder();
    auto nameLess = [](const ChipInfo& a, const ChipInfo& b) {
        return QString::compare(QString::fromStdString(a.tag.name()), QString::fromStdString(b.tag.name()),
                                 Qt::CaseInsensitive) < 0;
    };
    auto sortValue = [](const ChipInfo& info) { return info.hasChildren ? info.totalCount : info.ownCount; };

    std::sort(chipInfos.begin(), chipInfos.end(), [&](const ChipInfo& a, const ChipInfo& b) {
        if (sortKey == TagManagerViewModel::SortKey::Alphabetical)
        {
            return sortOrder == TagManagerViewModel::SortOrder::Ascending ? nameLess(a, b) : nameLess(b, a);
        }

        const int va = sortValue(a);
        const int vb = sortValue(b);
        if (va != vb)
        {
            return sortOrder == TagManagerViewModel::SortOrder::Ascending ? va < vb : va > vb;
        }
        return nameLess(a, b);
    });

    for (const ChipInfo& info : chipInfos)
    {
        std::optional<int> count;
        std::optional<int> totalCount;
        if (m_viewModel->showCounts())
        {
            count = info.ownCount;
            if (info.hasChildren)
            {
                totalCount = info.totalCount;
            }
        }

        auto* chip = new TagChipWidget(info.tag, TagChipWidget::Kind::Selectable, count, totalCount);
        connect(chip, &TagChipWidget::clicked, this, &TagManagerDialog::onChipClicked);
        m_chipLayout->addWidget(chip);
    }
}

namespace
{
// Plain local tree used by rebuildTagTree to compute counts/sort order before any
// QTreeWidgetItem exists (Architecture.md §14.92) -- children are held by unique_ptr so pointers
// into the tree stay valid across sibling insertion/vector growth.
struct TagTreeNode
{
    QString segment;
    std::optional<Tag::Id> tagId;
    int ownCount = 0;
    int totalCount = 0;
    std::vector<std::unique_ptr<TagTreeNode>> children;
};
} // namespace

void TagManagerDialog::rebuildTagTree(const std::vector<Tag>& tags)
{
    m_tagTree->clear();

    std::vector<std::unique_ptr<TagTreeNode>> roots;
    QHash<QString, TagTreeNode*> pathToNode;

    std::function<TagTreeNode*(const QString&)> ensurePath = [&](const QString& fullPath) -> TagTreeNode* {
        if (auto it = pathToNode.find(fullPath); it != pathToNode.end())
        {
            return it.value();
        }
        const int lastSlash = fullPath.lastIndexOf(QLatin1Char('/'));
        TagTreeNode* parent = lastSlash < 0 ? nullptr : ensurePath(fullPath.left(lastSlash));

        auto node = std::make_unique<TagTreeNode>();
        node->segment = fullPath.mid(lastSlash + 1);
        TagTreeNode* raw = node.get();
        if (parent)
        {
            parent->children.push_back(std::move(node));
        }
        else
        {
            roots.push_back(std::move(node));
        }
        pathToNode.insert(fullPath, raw);
        return raw;
    };

    for (const Tag& tag : tags)
    {
        TagTreeNode* node = ensurePath(QString::fromStdString(tag.name()));
        node->tagId = tag.id();
    }

    const bool needCounts = m_viewModel->countsNeeded();
    if (needCounts)
    {
        // Post-order: nested tags are a string-prefix display convention (Architecture.md
        // §14.9/§14.89), not a DB relation, so a parent's "total" is just the sum of its own count
        // (if it's a real tag, not just a path-grouping node) plus every descendant tag's own count.
        std::function<int(TagTreeNode&)> computeCounts = [&](TagTreeNode& node) -> int {
            int childrenTotal = 0;
            for (auto& child : node.children)
            {
                childrenTotal += computeCounts(*child);
            }
            node.ownCount = node.tagId.has_value() ? m_viewModel->countForTag(*node.tagId) : 0;
            node.totalCount = node.ownCount + childrenTotal;
            return node.totalCount;
        };

        for (auto& root : roots)
        {
            computeCounts(*root);
        }
    }

    const TagManagerViewModel::SortKey sortKey = m_viewModel->sortKey();
    const TagManagerViewModel::SortOrder sortOrder = m_viewModel->sortOrder();
    auto segmentLess = [](const TagTreeNode& a, const TagTreeNode& b) {
        return QString::compare(a.segment, b.segment, Qt::CaseInsensitive) < 0;
    };
    // A path-grouping node (e.g. "Project" when only "Project/Foo" is an actual tag) always has
    // at least one child, so a leaf's own count and an internal node's total are the right
    // like-for-like comparison per §14.92's sort rule.
    auto sortValue = [](const TagTreeNode& node) { return node.children.empty() ? node.ownCount : node.totalCount; };
    auto less = [&](const std::unique_ptr<TagTreeNode>& a, const std::unique_ptr<TagTreeNode>& b) {
        if (sortKey == TagManagerViewModel::SortKey::Alphabetical)
        {
            return sortOrder == TagManagerViewModel::SortOrder::Ascending ? segmentLess(*a, *b) : segmentLess(*b, *a);
        }

        const int va = sortValue(*a);
        const int vb = sortValue(*b);
        if (va != vb)
        {
            return sortOrder == TagManagerViewModel::SortOrder::Ascending ? va < vb : va > vb;
        }
        return segmentLess(*a, *b);
    };

    std::function<void(TagTreeNode&)> sortChildren = [&](TagTreeNode& node) {
        for (auto& child : node.children)
        {
            sortChildren(*child);
        }
        std::sort(node.children.begin(), node.children.end(), less);
    };
    for (auto& root : roots)
    {
        sortChildren(*root);
    }
    std::sort(roots.begin(), roots.end(), less);

    const bool showCounts = m_viewModel->showCounts();
    std::function<void(const TagTreeNode&, QTreeWidgetItem*)> buildItems = [&](const TagTreeNode& node,
                                                                                 QTreeWidgetItem* parent) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tagTree);

        QString text = node.segment;
        if (showCounts)
        {
            if (node.children.empty())
            {
                text += QStringLiteral("  (%1)").arg(node.ownCount);
            }
            else if (!node.tagId.has_value())
            {
                text += tr("  (%1 total)").arg(node.totalCount);
            }
            else
            {
                text += tr("  (%1, %2 total)").arg(node.ownCount).arg(node.totalCount);
            }
        }
        item->setText(0, text);
        item->setData(0, kIsVirtualRole, !node.tagId.has_value());
        if (node.tagId.has_value())
        {
            item->setData(0, kTagIdRole, static_cast<qlonglong>(*node.tagId));
        }

        for (const auto& child : node.children)
        {
            buildItems(*child, item);
        }
    };

    for (const auto& root : roots)
    {
        buildItems(*root, nullptr);
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

void TagManagerDialog::onSortSettingsChanged()
{
    m_viewModel->setSortKey(m_sortKeyCombo->currentIndex() == 1 ? TagManagerViewModel::SortKey::Count
                                                                 : TagManagerViewModel::SortKey::Alphabetical);
    m_viewModel->setSortOrder(m_sortOrderCombo->currentIndex() == 1 ? TagManagerViewModel::SortOrder::Descending
                                                                     : TagManagerViewModel::SortOrder::Ascending);
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
    settings.setValue(QLatin1String(kSortKeySettingsKey), m_sortKeyCombo->currentIndex());
    settings.setValue(QLatin1String(kSortOrderSettingsKey), m_sortOrderCombo->currentIndex());

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
