#pragma once

#include <QListView>

class FilePreview;
class FolderPreviewListModel;

// The folder page of the Preview panel's stack (Architecture.md §14.26/§14.58, Specification.md
// "Preview Panel"): a flat one-level folder listing using existing QFileIconProvider icons,
// backed by FolderPreviewListModel so per-entry icon lookups (expensive for executables) only
// happen for rows the view actually paints.
class FolderPreviewWidget : public QListView
{
    Q_OBJECT

public:
    explicit FolderPreviewWidget(QWidget* parent = nullptr);

    void setPreview(const FilePreview& preview);

private:
    FolderPreviewListModel* m_model = nullptr;
};
