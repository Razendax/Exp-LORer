#pragma once

#include <vector>

#include <QImage>
#include <QMetaType>
#include <QString>

// Media Viewer mode's Folder-Grouped Browsing (Architecture.md §14.74): a subfolder tile's 2x2
// preview collage + item-count badge. FolderPreviewSeed is what TabViewModel::buildFolderGroupedView
// records per synthesized directory row -- itemCount plus up to 4 sample file paths (in the
// current sort order) to draw the collage from. MediaFolderPreviewData is what
// FileListModel::data()'s FolderPreviewRole resolves a seed into on demand, by looking its sample
// paths up against the already-decoded thumbnail cache (m_mediaThumbnailsByPath) -- 0-4 entries,
// only whichever samples have decoded so far, the same "placeholder now, real data later"
// relationship MediaThumbnailRole already has.
struct FolderPreviewSeed
{
    int itemCount = 0;
    std::vector<QString> samplePaths;
};

struct MediaFolderPreviewData
{
    int itemCount = 0;
    std::vector<QImage> previewImages;
};

Q_DECLARE_METATYPE(MediaFolderPreviewData)
