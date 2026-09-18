#pragma once

#include "FileNameEditDelegate.h"

// Paints icon + name-underneath for the four Icon-size view modes (Extra Large/Large/Medium/Small,
// Architecture.md §2.3.1) — swapped in as the QListView's item delegate only while one of those
// modes is active. Selection is drawn as a dashed rectangle outline instead of Qt's default filled
// highlight; List mode keeps Qt's stock delegate. Inherits inline-rename editing from
// FileNameEditDelegate; only the editor's on-screen geometry needs overriding here, to match the
// name-caption rect paint() computes below.
class FileIconDelegate : public FileNameEditDelegate
{
    Q_OBJECT

public:
    explicit FileIconDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    static QRect nameRectFor(const QStyleOptionViewItem& option);
};
