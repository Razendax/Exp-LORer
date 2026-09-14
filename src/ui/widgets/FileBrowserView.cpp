#include "FileBrowserView.h"

#include <QEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include "FileListModel.h"
#include "FileTileDelegate.h"

namespace
{
    // Icon-mode sizing steps, matching Explorer's own steps (Architecture.md §2.3.1).
    QSize iconSizeFor(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::ExtraLargeIcons:
                return QSize(256, 256);
            case ViewMode::LargeIcons:
                return QSize(96, 96);
            case ViewMode::MediumIcons:
                return QSize(48, 48);
            case ViewMode::Tiles:
                return QSize(48, 48);
            case ViewMode::SmallIcons:
            case ViewMode::List:
            case ViewMode::Details:
                return QSize(16, 16);
        }
        return QSize(48, 48);
    }
}

FileBrowserView::FileBrowserView(FileListModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    m_listView = new QListView(this);
    m_listView->setModel(m_model);
    m_defaultDelegate = m_listView->itemDelegate();
    m_tileDelegate = new FileTileDelegate(this);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setRootIsDecorated(false);
    m_treeView->setSortingEnabled(true);
    m_treeView->header()->setSectionsClickable(true);

    // Shared between both views (rather than each QAbstractItemView's own default selection
    // model) so switching ViewMode doesn't drop the current selection.
    m_selectionModel = new QItemSelectionModel(m_model, this);
    m_listView->setSelectionModel(m_selectionModel);
    m_treeView->setSelectionModel(m_selectionModel);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_listView);
    m_stack->addWidget(m_treeView);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    connect(m_listView, &QAbstractItemView::activated, this, &FileBrowserView::emitActivated);
    connect(m_treeView, &QAbstractItemView::activated, this, &FileBrowserView::emitActivated);
    connect(m_selectionModel, &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) { emitSelectionChanged(current); });

    m_listView->installEventFilter(this);
    m_treeView->installEventFilter(this);

    setViewMode(ViewMode::Details);
}

bool FileBrowserView::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_listView || watched == m_treeView) && event->type() == QEvent::KeyPress)
    {
        if (handleKeyPress(static_cast<QKeyEvent*>(event)))
        {
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

bool FileBrowserView::handleKeyPress(QKeyEvent* event)
{
    // Must be checked before QKeySequence::Cut: Qt's standard Cut sequence on Windows includes
    // "Shift+Del" as an alternate binding, which file managers repurpose for permanent delete.
    if (event->key() == Qt::Key_Delete)
    {
        emit deleteRequested(event->modifiers().testFlag(Qt::ShiftModifier));
        return true;
    }
    if (event->matches(QKeySequence::Copy))  { emit copyRequested();  return true; }
    if (event->matches(QKeySequence::Cut))   { emit cutRequested();   return true; }
    if (event->matches(QKeySequence::Paste)) { emit pasteRequested(); return true; }
    if (event->key() == Qt::Key_Backspace)   { emit navigateUpRequested(); return true; }

    const bool isDetailsView = (m_stack->currentWidget() == m_treeView);
    if (isDetailsView && event->key() == Qt::Key_Left)
    {
        emit navigateUpRequested();
        return true;
    }
    if (isDetailsView && event->key() == Qt::Key_Right)
    {
        const QModelIndex current = m_selectionModel->currentIndex();
        if (current.isValid() && current.data(FileListModel::IsDirectoryRole).toBool())
        {
            emitActivated(current);
        }
        return true; // consumed either way - no-op on a file
    }

    return false; // unhandled: falls through to Qt's default keyboardSearch() etc.
}

void FileBrowserView::setViewMode(ViewMode mode)
{
    if (mode == ViewMode::Details)
    {
        m_stack->setCurrentWidget(m_treeView);
        return;
    }

    m_stack->setCurrentWidget(m_listView);
    m_listView->setIconSize(iconSizeFor(mode));
    m_listView->setResizeMode(QListView::Adjust);

    if (mode == ViewMode::Tiles)
    {
        m_listView->setItemDelegate(m_tileDelegate);
        m_listView->setViewMode(QListView::IconMode);
        m_listView->setFlow(QListView::LeftToRight);
        m_listView->setWrapping(true);
        m_listView->setUniformItemSizes(true);
        m_listView->setGridSize(QSize(220, 56));
        return;
    }

    m_listView->setItemDelegate(m_defaultDelegate);
    m_listView->setUniformItemSizes(true);

    if (mode == ViewMode::List)
    {
        m_listView->setViewMode(QListView::ListMode);
        m_listView->setFlow(QListView::TopToBottom);
        m_listView->setWrapping(true);
        m_listView->setGridSize(QSize());
        return;
    }

    // ExtraLargeIcons / LargeIcons / MediumIcons / SmallIcons
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setGridSize(QSize());
}

void FileBrowserView::emitActivated(const QModelIndex& index)
{
    if (!index.isValid())
    {
        return;
    }

    const auto path = std::filesystem::path(index.data(FileListModel::FilePathRole).toString().toStdWString());
    const bool isDirectory = index.data(FileListModel::IsDirectoryRole).toBool();
    emit itemActivated(path, isDirectory);
}

void FileBrowserView::emitSelectionChanged(const QModelIndex& current)
{
    emit selectionChanged(m_model->entryAt(current.row()));
}
