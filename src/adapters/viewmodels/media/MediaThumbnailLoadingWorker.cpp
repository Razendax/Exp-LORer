#include "MediaThumbnailLoadingWorker.h"

#include <filesystem>
#include <optional>

#include <QImage>

#include "MediaExtensions.h"
#include "MediaMetadata.h"
#include "MediaPreviewFrames.h"
#include "MediaProcessingUseCase.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    std::optional<QImage> decodeImage(const std::vector<std::byte>& bytes)
    {
        QImage image;
        if (!image.loadFromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<int>(bytes.size())))
        {
            return std::nullopt;
        }
        return image;
    }

    // Decodes every one of MediaPreviewFrames::kCount hover-scrub frames for a video entry
    // (Architecture.md §14.64), backfilling any single failed slot from its nearest
    // successfully-decoded neighbor so scrubbing never hits a blank frame. Returns std::nullopt
    // (omit the row entirely, same "placeholder keeps showing" posture as an image decode failure)
    // only if every slot fails.
    std::optional<MediaThumbnailData> decodeVideoThumbnail(MediaProcessingUseCase& useCase, const std::filesystem::path& path,
                                                              int maxWidth, int maxHeight)
    {
        MediaThumbnailData data;
        data.previewFrames.resize(MediaPreviewFrames::kCount);

        std::vector<int> previewIndices(MediaPreviewFrames::kCount);
        for (int i = 0; i < MediaPreviewFrames::kCount; ++i)
        {
            previewIndices[static_cast<std::size_t>(i)] = i;
        }
        const std::vector<Result<MediaThumbnail>> thumbnails = useCase.generateThumbnails(path, maxWidth, maxHeight, previewIndices);

        bool anySucceeded = false;
        for (int i = 0; i < MediaPreviewFrames::kCount; ++i)
        {
            const Result<MediaThumbnail>& thumbnail = thumbnails[static_cast<std::size_t>(i)];
            if (thumbnail.hasError())
            {
                continue;
            }

            std::optional<QImage> image = decodeImage(thumbnail.value().bytes);
            if (!image)
            {
                continue;
            }

            data.previewFrames[static_cast<std::size_t>(i)] = std::move(*image);
            if (!anySucceeded)
            {
                data.width = thumbnail.value().metadata.width();
                data.height = thumbnail.value().metadata.height();
                data.duration = thumbnail.value().metadata.duration();
            }
            anySucceeded = true;
        }

        if (!anySucceeded)
        {
            return std::nullopt;
        }

        // Backfill any still-null slot from its nearest successfully-decoded neighbor.
        for (int i = 0; i < MediaPreviewFrames::kCount; ++i)
        {
            if (!data.previewFrames[static_cast<std::size_t>(i)].isNull())
            {
                continue;
            }
            for (int offset = 1; offset < MediaPreviewFrames::kCount; ++offset)
            {
                const int left = i - offset;
                const int right = i + offset;
                if (left >= 0 && !data.previewFrames[static_cast<std::size_t>(left)].isNull())
                {
                    data.previewFrames[static_cast<std::size_t>(i)] = data.previewFrames[static_cast<std::size_t>(left)];
                    break;
                }
                if (right < MediaPreviewFrames::kCount && !data.previewFrames[static_cast<std::size_t>(right)].isNull())
                {
                    data.previewFrames[static_cast<std::size_t>(i)] = data.previewFrames[static_cast<std::size_t>(right)];
                    break;
                }
            }
        }

        data.image = data.previewFrames[0];
        return data;
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
        const std::string extension = MediaExtensions::lowercaseExtension(entry.path());

        if (MediaExtensions::isVideoExtension(extension))
        {
            std::optional<MediaThumbnailData> data =
                decodeVideoThumbnail(m_mediaProcessingUseCase, entry.path(), maxWidth, maxHeight);
            if (data)
            {
                result.insert(toQString(entry.path()), std::move(*data));
            }
            continue;
        }

        const Result<MediaThumbnail> thumbnail = m_mediaProcessingUseCase.generateThumbnail(entry.path(), maxWidth, maxHeight);
        if (thumbnail.hasError())
        {
            continue;
        }

        std::optional<QImage> image = decodeImage(thumbnail.value().bytes);
        if (!image)
        {
            continue;
        }

        MediaThumbnailData data;
        data.image = std::move(*image);
        data.width = thumbnail.value().metadata.width();
        data.height = thumbnail.value().metadata.height();
        data.duration = thumbnail.value().metadata.duration();

        result.insert(toQString(entry.path()), std::move(data));
    }

    return result;
}
