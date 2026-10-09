#include "MediaThumbnailLoadingWorker.h"

#include <filesystem>

#include <QImage>

#include "MediaMetadata.h"
#include "MediaProcessingUseCase.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

MediaThumbnailLoadingWorker::MediaThumbnailLoadingWorker(MediaProcessingUseCase& mediaProcessingUseCase, QObject* parent)
    : QObject(parent)
    , m_mediaProcessingUseCase(mediaProcessingUseCase)
{
}

QHash<QString, MediaThumbnailData> MediaThumbnailLoadingWorker::thumbnailsFor(const std::vector<FileNode>& entries, int maxWidth,
                                                                               int maxHeight) const
{
    QHash<QString, MediaThumbnailData> result;
    result.reserve(static_cast<int>(entries.size()));

    for (const FileNode& entry : entries)
    {
        const Result<std::vector<std::byte>> thumbnail = m_mediaProcessingUseCase.generateThumbnail(entry.path(), maxWidth, maxHeight);
        if (thumbnail.hasError())
        {
            continue;
        }

        QImage image;
        if (!image.loadFromData(reinterpret_cast<const uchar*>(thumbnail.value().data()), static_cast<int>(thumbnail.value().size())))
        {
            continue;
        }

        MediaThumbnailData data;
        data.image = std::move(image);

        const Result<MediaMetadata> metadata = m_mediaProcessingUseCase.extractMetadata(entry.path());
        if (!metadata.hasError())
        {
            data.width = metadata.value().width();
            data.height = metadata.value().height();
            data.duration = metadata.value().duration();
        }

        result.insert(toQString(entry.path()), std::move(data));
    }

    return result;
}
