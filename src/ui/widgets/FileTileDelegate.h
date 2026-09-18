#pragma once

#include "FileNameEditDelegate.h"

// Paints icon + two lines of text (name, then type/size) for Tiles view mode
// (Architecture.md §2.3.1) — Qt has no built-in tile layout, so FileBrowserView swaps this in as
// the QListView's item delegate only while Tiles is the active view mode. Inherits inline-rename
// editing from FileNameEditDelegate; only the editor's on-screen geometry needs overriding here,
// to match the name-line rect paint() computes below.
class FileTileDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit FileTileDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option);
};
