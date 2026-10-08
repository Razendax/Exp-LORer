#include "FolderPreviewWidget.h"

#include "FilePreview.h"
#include "FolderPreviewListModel.h"

FolderPreviewWidget::FolderPreviewWidget(QWidget* parent)
    : QListView(parent)
{
    setObjectName(QStringLiteral("previewFolderList"));
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setIconSize(QSize(16, 16));
    setUniformItemSizes(true);
    m_model = new FolderPreviewListModel(this);
    setModel(m_model);
}

void FolderPreviewWidget::setPreview(const FilePreview& preview)
{
    m_model->setEntries(preview.folderEntries());
}
