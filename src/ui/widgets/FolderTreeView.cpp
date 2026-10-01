#include "FolderTreeView.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QShowEvent>

#include "FileNameEditDelegate.h"
#include "FolderTreeModel.h"

FolderTreeView::FolderTreeView(FolderTreeModel* model, QWidget* parent)
    : QTreeView(parent)
    , m_model(model)
{
    setModel(model);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformRowHeights(true);
    setAnimated(false);

    // Rename only ever starts programmatically (F2 / context menu -> beginRename()), never from a
    // click: a single click already means "navigate here".
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Same inline editor as the main view: model column 0 lines up with FileListModel::NameColumn,
    // so its forbidden-character validator and sibling-collision check work unchanged.
    auto* delegate = new FileNameEditDelegate(this);
    setItemDelegate(delegate);
    connect(delegate, &FileNameEditDelegate::renameCommitted, this, &FolderTreeView::onRenameCommitted);

    // QTreeView doesn't emit clicked() for a press on the expand arrow (it resets its pressed index
    // there), so the arrow only expands/collapses and a click on the row itself navigates.
    connect(this, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
        {
            emit navigateRequested(m_model->pathForIndex(index));
        }
    });

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &FolderTreeView::onCustomContextMenuRequested);

    connect(m_model, &FolderTreeModel::activePathChanged, this, &FolderTreeView::revealPath);
}

void FolderTreeView::beginRename(const std::filesystem::path& path)
{
    const QModelIndex index = m_model->indexForPath(path);
    if (!index.isValid() || !m_model->flags(index).testFlag(Qt::ItemIsEditable))
    {
        return;
    }

    setCurrentIndex(index);
    scrollTo(index);
    edit(index);
}

void FolderTreeView::revealPath(const std::filesystem::path& path)
{
    if (!isVisible())
    {
        m_revealPending = true;
        return;
    }
    m_revealPending = false;

    const QModelIndex index = m_model->indexForPath(path);
    if (!index.isValid())
    {
        clearSelection();
        return;
    }

    for (QModelIndex ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent())
    {
        expand(ancestor);
    }
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void FolderTreeView::keyPressEvent(QKeyEvent* event)
{
    const QModelIndex current = currentIndex();
    const bool editable = current.isValid() && m_model->flags(current).testFlag(Qt::ItemIsEditable);

    switch (event->key())
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (current.isValid())
            {
                emit navigateRequested(m_model->pathForIndex(current));
            }
            return;
        case Qt::Key_F2:
            if (editable)
            {
                edit(current);
            }
            return;
        case Qt::Key_Delete:
            if (editable)
            {
                emit deleteRequested(m_model->pathForIndex(current), event->modifiers().testFlag(Qt::ShiftModifier));
            }
            return;
        case Qt::Key_F5:
            if (current.isValid())
            {
                m_model->refreshDirectory(m_model->pathForIndex(current));
            }
            return;
        default:
            QTreeView::keyPressEvent(event);
    }
}

void FolderTreeView::showEvent(QShowEvent* event)
{
    QTreeView::showEvent(event);

    if (m_revealPending)
    {
        revealPath(m_model->activePath());
    }
}

void FolderTreeView::onRenameCommitted(const QModelIndex& index, const QString& newName)
{
    const std::filesystem::path source = m_model->pathForIndex(index);
    emit renameRequested(source, source.parent_path() / std::filesystem::path(newName.toStdWString()));
}

void FolderTreeView::onCustomContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = indexAt(pos);
    // No menu on empty space or on "This PC" itself (Architecture.md §14.41).
    if (!index.isValid() || !index.parent().isValid())
    {
        return;
    }

    emit itemContextMenuRequested(m_model->pathForIndex(index), m_model->isTopLevelLocation(index),
                                  viewport()->mapToGlobal(pos));
}
