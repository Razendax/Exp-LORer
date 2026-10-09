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
        const Result<MediaThumbnail> thumbnail = m_mediaProcessingUseCase.generateThumbnail(entry.path(), maxWidth, maxHeight);
        if (thumbnail.hasError())
        {
            continue;
        }

        const std::vector<std::byte>& bytes = thumbnail.value().bytes;
        QImage image;
        if (!image.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<int>(bytes.size())))
        {
            continue;
        }

        MediaThumbnailData data;
        data.image = std::move(image);
        data.width = thumbnail.value().metadata.width();
        data.height = thumbnail.value().metadata.height();
        data.duration = thumbnail.value().metadata.duration();

        result.insert(toQString(entry.path()), std::move(data));
    }

    return result;
}
