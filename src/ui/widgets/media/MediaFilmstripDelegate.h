#pragma once

#include <QStyledItemDelegate>

// Media Viewer mode's "Filmstrip" view (Architecture.md §14.69): one full-width row per item,
// showing every decoded preview frame (MediaPreviewFrames::kCount of them, Architecture.md
// §14.64) side-by-side with a thin divider between each, name caption centered underneath. An
// image (or a video not yet fully decoded) paints its single image stretched across the whole
// row instead. Installed as m_listView's delegate only while
// FileBrowserView::setMediaViewerViewMode(Filmstrip) is active -- no hover-scrub state of its own,
// since every frame is already visible at once.
class MediaFilmstripDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MediaFilmstripDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
