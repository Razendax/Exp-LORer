#pragma once

#include "FileNameEditDelegate.h"
#include "ViewMode.h"

// Paints icon + name-underneath for the four Icon-size view modes (Extra Large/Large/Medium/Small,
// Architecture.md §2.3.1) — swapped in as the QListView's item delegate only while one of those
// modes is active. Selection is drawn as a dashed rectangle outline instead of Qt's default filled
// highlight; List mode keeps Qt's stock delegate. Inherits inline-rename editing from
// FileNameEditDelegate; only the editor's on-screen geometry needs overriding here, to match the
// name-caption rect paint() computes below.
//
// Extra Large Icons only (Architecture.md §14.71): a non-directory image/video row paints a real
// decoded thumbnail (FileListModel::MediaThumbnailRole, the same payload/pipeline Media Viewer
// grid's MediaGridTileDelegate already uses) instead of the plain Shell icon, falling back to the
// Shell icon until the thumbnail arrives or for every other icon-size tier. setViewMode() is how
// FileBrowserView tells this delegate which tier is currently active.
class FileIconDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit FileIconDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setViewMode(ViewMode mode) { m_viewMode = mode; }

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option, const QModelIndex& index);

    ViewMode m_viewMode = ViewMode::Details;
};
