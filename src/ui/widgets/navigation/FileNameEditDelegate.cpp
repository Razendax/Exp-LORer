#include "FileNameEditDelegate.h"

#include <QLineEdit>
#include <QPoint>
#include <QPointer>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QToolTip>

#include "FileListModel.h"

namespace
{
    // Windows-reserved filename characters, blocked as the user types (Architecture.md §14.13.4).
    // Anything else invalid (reserved device names, trailing dot/space) still surfaces through
    // FileOperationsController::renamePath -> operationFailed. Name collisions are checked
    // up-front instead (see nameCollidesWithSibling) since the destination path is known here.
    const QRegularExpression& forbiddenCharsPattern()
    {
        static const QRegularExpression pattern(QStringLiteral("^[^\\\\/:*?\"<>|]*$"));
        return pattern;
    }

    // True if some other entry in the same directory already has newName. Windows filesystems are
    // case-insensitive, so this is a case-insensitive comparison regardless of view sort order.
    bool nameCollidesWithSibling(const QAbstractItemModel* model, const QModelIndex& index, const QString& newName)
    {
        const QModelIndex parent = index.parent();
        const int rows = model->rowCount(parent);
        for (int row = 0; row < rows; ++row)
        {
            if (row == index.row())
            {
                continue;
            }

            const QModelIndex sibling = model->index(row, FileListModel::NameColumn, parent);
            if (sibling.data(Qt::DisplayRole).toString().compare(newName, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
    }
}

FileNameEditDelegate::FileNameEditDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QWidget* FileNameEditDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.column() != FileListModel::NameColumn)
    {
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    auto* editor = new QLineEdit(parent);
    editor->setValidator(new QRegularExpressionValidator(forbiddenCharsPattern(), editor));
    return editor;
}

void FileNameEditDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
    {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }

    // FileListModel has no Qt::EditRole handling of its own (and, unlike QStyledItemDelegate's
    // default setEditorData(), overriding this method doesn't get Qt's usual EditRole ->
    // DisplayRole fallback for free) -- DisplayRole is already exactly the filename for NameColumn.
    const QString name = index.data(Qt::DisplayRole).toString();
    lineEdit->setText(name);

    // Select the stem (Explorer behavior): whole name for directories, dotfiles, and
    // extension-less files; otherwise everything before the last extension's dot.
    const bool isDirectory = index.data(FileListModel::IsDirectoryRole).toBool();
    const int dotIndex = name.lastIndexOf(QLatin1Char('.'));

    const auto applySelection = [lineEdit, isDirectory, dotIndex]() {
        if (!isDirectory && dotIndex > 0)
        {
            lineEdit->setSelection(0, dotIndex);
        }
        else
        {
            lineEdit->selectAll();
        }
    };

    applySelection();

    // QAbstractItemView::edit() calls QWidget::show()/setFocus() on the freshly created editor
    // *before* invoking setEditorData(), so the selection set above is applied first -- but the
    // platform style's own focus-in handling on the QLineEdit can still clobber it once the event
    // loop processes that pending focus event. Re-apply once more via a queued call so ours is the
    // one that sticks.
    QPointer<QLineEdit> guardedLineEdit(lineEdit);
    QTimer::singleShot(0, lineEdit, [guardedLineEdit, applySelection]() {
        if (guardedLineEdit)
        {
            applySelection();
        }
    });
}

void FileNameEditDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    Q_UNUSED(model);

    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
    {
        return;
    }

    const QString newName = lineEdit->text().trimmed();
    const QString currentName = index.data(Qt::DisplayRole).toString();
    if (newName.isEmpty() || newName == currentName)
    {
        return;
    }

    if (nameCollidesWithSibling(model, index, newName))
    {
        QToolTip::showText(lineEdit->mapToGlobal(QPoint(0, lineEdit->height())),
                            tr("An item named \"%1\" already exists here.").arg(newName), lineEdit);
        return;
    }

    // setModelData() is const (QStyledItemDelegate's signature); the signal emission itself
    // doesn't touch *this* delegate's own state.
    emit const_cast<FileNameEditDelegate*>(this)->renameCommitted(index, newName);
}
