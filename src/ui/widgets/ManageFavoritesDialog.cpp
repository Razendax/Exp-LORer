#include "ManageFavoritesDialog.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "FavoritesViewModel.h"

namespace
{
    constexpr int kIdRole = Qt::UserRole;
    constexpr int kIsCategoryRole = Qt::UserRole + 1;
    // Full path shown as a second, grayed/smaller line under an item's name -- unset (invalid
    // QVariant, so QVariant::toString() reads back empty) on category rows, which have no path.
    constexpr int kPathRole = Qt::UserRole + 2;

    Favorites::Id idOf(const QTreeWidgetItem* item)
    {
        return static_cast<Favorites::Id>(item->data(0, kIdRole).toLongLong());
    }

    bool isCategoryRow(const QTreeWidgetItem* item)
    {
        return item->data(0, kIsCategoryRole).toBool();
    }

    // Same QFileIconProvider-based lookup MainWindow's Favorites menu uses.
    QIcon favoriteItemIcon(const std::filesystem::path& path)
    {
        static QFileIconProvider provider;
        return provider.icon(QFileInfo(QString::fromStdWString(path.wstring())));
    }

    QIcon favoriteCategoryIcon()
    {
        static QFileIconProvider provider;
        return provider.icon(QFileIconProvider::Folder);
    }

    // Paints a row's name plus (when kPathRole is non-empty) a second line below it with the full
    // path, at 0.85x the name's point size and in gray -- same secondary-line scale factor
    // FileTileDelegate.cpp uses for its type/size line. Rows without a path (categories) fall back
    // to the base single-line rendering untouched.
    class FavoriteTreeItemDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            const QString path = index.data(kPathRole).toString();
            if (path.isEmpty())
            {
                QStyledItemDelegate::paint(painter, option, index);
                return;
            }

            painter->save();

            const bool selected = option.state & QStyle::State_Selected;
            if (selected)
            {
                painter->fillRect(option.rect, option.palette.highlight());
            }

            constexpr int kIconSize = 16;
            constexpr int kPadding = 3;
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            const QRect iconRect(option.rect.left() + kPadding,
                                  option.rect.top() + (option.rect.height() - kIconSize) / 2,
                                  kIconSize,
                                  kIconSize);
            icon.paint(painter, iconRect);

            const QRect textRect(iconRect.right() + kPadding,
                                  option.rect.top(),
                                  option.rect.right() - iconRect.right() - kPadding,
                                  option.rect.height());

            const QFont nameFont = option.font;
            const QFontMetrics nameMetrics(nameFont);

            QFont pathFont = nameFont;
            pathFont.setPointSizeF(pathFont.pointSizeF() * 0.85);
            const QFontMetrics pathMetrics(pathFont);

            const int startY = option.rect.top() + (option.rect.height() - nameMetrics.height() - pathMetrics.height()) / 2;
            const QRect nameRect(textRect.left(), startY, textRect.width(), nameMetrics.height());
            const QRect pathRect(textRect.left(), nameRect.bottom(), textRect.width(), pathMetrics.height());

            const QString name = index.data(Qt::DisplayRole).toString();
            painter->setFont(nameFont);
            painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
            painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));

            painter->setFont(pathFont);
            painter->setPen(selected ? option.palette.highlightedText().color() : QColor(Qt::gray));
            painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignVCenter, pathMetrics.elidedText(path, Qt::ElideRight, pathRect.width()));

            painter->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            const QString path = index.data(kPathRole).toString();
            if (path.isEmpty())
            {
                return QStyledItemDelegate::sizeHint(option, index);
            }

            const QFontMetrics nameMetrics(option.font);
            QFont pathFont = option.font;
            pathFont.setPointSizeF(pathFont.pointSizeF() * 0.85);
            const QFontMetrics pathMetrics(pathFont);

            const QSize base = QStyledItemDelegate::sizeHint(option, index);
            return QSize(base.width(), nameMetrics.height() + pathMetrics.height() + 6);
        }
    };
}

ManageFavoritesDialog::ManageFavoritesDialog(FavoritesViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setObjectName(QStringLiteral("manageFavoritesDialog"));
    setWindowTitle(tr("Manage Favorites"));
    resize(480, 420);

    auto* mainLayout = new QVBoxLayout(this);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("manageFavoritesTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setItemDelegate(new FavoriteTreeItemDelegate(m_tree));
    mainLayout->addWidget(m_tree, 1);

    auto* buttonLayout = new QHBoxLayout();

    auto* addFileButton = new QPushButton(tr("Add File..."), this);
    addFileButton->setObjectName(QStringLiteral("manageFavoritesAddFileButton"));
    auto* addFolderButton = new QPushButton(tr("Add Folder..."), this);
    addFolderButton->setObjectName(QStringLiteral("manageFavoritesAddFolderButton"));
    auto* addCategoryButton = new QPushButton(tr("Add Category"), this);
    addCategoryButton->setObjectName(QStringLiteral("manageFavoritesAddCategoryButton"));
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_renameButton->setObjectName(QStringLiteral("manageFavoritesRenameButton"));
    m_moveToCategoryButton = new QPushButton(tr("Move to Category..."), this);
    m_moveToCategoryButton->setObjectName(QStringLiteral("manageFavoritesMoveToCategoryButton"));
    m_moveUpButton = new QPushButton(tr("Move Up"), this);
    m_moveUpButton->setObjectName(QStringLiteral("manageFavoritesMoveUpButton"));
    m_moveDownButton = new QPushButton(tr("Move Down"), this);
    m_moveDownButton->setObjectName(QStringLiteral("manageFavoritesMoveDownButton"));
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setObjectName(QStringLiteral("manageFavoritesRemoveButton"));
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("manageFavoritesCloseButton"));

    buttonLayout->addWidget(addFileButton);
    buttonLayout->addWidget(addFolderButton);
    buttonLayout->addWidget(addCategoryButton);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_moveToCategoryButton);
    buttonLayout->addWidget(m_moveUpButton);
    buttonLayout->addWidget(m_moveDownButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) { onSelectionChanged(); });
    connect(addFileButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onAddFileClicked);
    connect(addFolderButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onAddFolderClicked);
    connect(addCategoryButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onAddCategoryClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onRenameClicked);
    connect(m_moveToCategoryButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onMoveToCategoryClicked);
    connect(m_moveUpButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onMoveUpClicked);
    connect(m_moveDownButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onMoveDownClicked);
    connect(m_removeButton, &QPushButton::clicked, this, &ManageFavoritesDialog::onRemoveClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_viewModel, &FavoritesViewModel::favoritesChanged, this, &ManageFavoritesDialog::rebuildTree);

    rebuildTree();
}

void ManageFavoritesDialog::rebuildTree()
{
    m_tree->clear();

    for (const Favorites::Entry& entry : m_viewModel->entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry))
        {
            auto* row = new QTreeWidgetItem(m_tree);
            row->setText(0, QString::fromStdWString(item->displayName().wstring()));
            row->setIcon(0, favoriteItemIcon(item->path()));
            row->setToolTip(0, QString::fromStdWString(item->path().wstring()));
            row->setData(0, kPathRole, QString::fromStdWString(item->path().wstring()));
            row->setData(0, kIdRole, static_cast<qlonglong>(item->id()));
            row->setData(0, kIsCategoryRole, false);
            continue;
        }

        const auto* category = std::get_if<FavoriteCategory>(&entry);
        auto* categoryRow = new QTreeWidgetItem(m_tree);
        categoryRow->setText(0, QString::fromStdString(category->name()));
        categoryRow->setIcon(0, favoriteCategoryIcon());
        categoryRow->setData(0, kIdRole, static_cast<qlonglong>(category->id()));
        categoryRow->setData(0, kIsCategoryRole, true);

        for (const FavoriteItem& categoryItem : category->items())
        {
            auto* itemRow = new QTreeWidgetItem(categoryRow);
            itemRow->setText(0, QString::fromStdWString(categoryItem.displayName().wstring()));
            itemRow->setIcon(0, favoriteItemIcon(categoryItem.path()));
            itemRow->setToolTip(0, QString::fromStdWString(categoryItem.path().wstring()));
            itemRow->setData(0, kPathRole, QString::fromStdWString(categoryItem.path().wstring()));
            itemRow->setData(0, kIdRole, static_cast<qlonglong>(categoryItem.id()));
            itemRow->setData(0, kIsCategoryRole, false);
        }

        categoryRow->setExpanded(true);
    }

    onSelectionChanged();
}

void ManageFavoritesDialog::onSelectionChanged()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current)
    {
        m_renameButton->setEnabled(false);
        m_moveToCategoryButton->setEnabled(false);
        m_moveUpButton->setEnabled(false);
        m_moveDownButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    const bool isCategory = isCategoryRow(current);
    QTreeWidgetItem* parent = current->parent();
    const int index = parent ? parent->indexOfChild(current) : m_tree->indexOfTopLevelItem(current);
    const int siblingCount = parent ? parent->childCount() : m_tree->topLevelItemCount();

    m_renameButton->setEnabled(isCategory);
    m_moveToCategoryButton->setEnabled(!isCategory);
    m_moveUpButton->setEnabled(index > 0);
    m_moveDownButton->setEnabled(index + 1 < siblingCount);
    m_removeButton->setEnabled(true);
}

void ManageFavoritesDialog::onAddFileClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Add File"));
    if (path.isEmpty())
    {
        return;
    }
    m_viewModel->addItem(std::filesystem::path(path.toStdWString()));
}

void ManageFavoritesDialog::onAddFolderClicked()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
    if (path.isEmpty())
    {
        return;
    }
    m_viewModel->addItem(std::filesystem::path(path.toStdWString()));
}

void ManageFavoritesDialog::onAddCategoryClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add Category"), tr("Category name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }
    m_viewModel->addCategory(name.trimmed());
}

void ManageFavoritesDialog::onRenameClicked()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current || !isCategoryRow(current))
    {
        return;
    }

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("Rename Category"), tr("New name:"), QLineEdit::Normal, current->text(0), &ok);
    if (!ok || newName.trimmed().isEmpty())
    {
        return;
    }

    m_viewModel->renameCategory(idOf(current), newName.trimmed());
}

void ManageFavoritesDialog::onMoveToCategoryClicked()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current || isCategoryRow(current))
    {
        return;
    }

    const Favorites::Id itemId = idOf(current);

    QMenu menu(this);
    QAction* noneAction = menu.addAction(tr("(None)"));
    menu.addSeparator();

    std::vector<std::pair<QAction*, Favorites::Id>> categoryActions;
    for (const Favorites::Entry& entry : m_viewModel->entries())
    {
        if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            QAction* action = menu.addAction(QString::fromStdString(category->name()));
            categoryActions.emplace_back(action, category->id());
        }
    }

    QAction* chosen = menu.exec(m_moveToCategoryButton->mapToGlobal(QPoint(0, m_moveToCategoryButton->height())));
    if (!chosen)
    {
        return;
    }

    if (chosen == noneAction)
    {
        m_viewModel->moveItemToCategory(itemId, std::nullopt);
        return;
    }

    for (const auto& [action, categoryId] : categoryActions)
    {
        if (action == chosen)
        {
            m_viewModel->moveItemToCategory(itemId, categoryId);
            return;
        }
    }
}

void ManageFavoritesDialog::onMoveUpClicked()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current)
    {
        return;
    }

    if (QTreeWidgetItem* parent = current->parent())
    {
        m_viewModel->moveItemUpWithinCategory(idOf(parent), idOf(current));
    }
    else
    {
        m_viewModel->moveEntryUp(idOf(current));
    }
}

void ManageFavoritesDialog::onMoveDownClicked()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current)
    {
        return;
    }

    if (QTreeWidgetItem* parent = current->parent())
    {
        m_viewModel->moveItemDownWithinCategory(idOf(parent), idOf(current));
    }
    else
    {
        m_viewModel->moveEntryDown(idOf(current));
    }
}

void ManageFavoritesDialog::onRemoveClicked()
{
    QTreeWidgetItem* current = m_tree->currentItem();
    if (!current)
    {
        return;
    }

    if (QTreeWidgetItem* parent = current->parent())
    {
        m_viewModel->removeItemFromCategory(idOf(parent), idOf(current));
    }
    else
    {
        m_viewModel->removeEntry(idOf(current));
    }
}
