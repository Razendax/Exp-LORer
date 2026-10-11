#include "IMediaDecoder.h"

std::vector<Result<MediaThumbnail>> IMediaDecoder::generateThumbnails(const std::filesystem::path& mediaFile, int maxWidth,
                                                                        int maxHeight, const std::vector<int>& previewIndices) const
{
    std::vector<Result<MediaThumbnail>> results;
    results.reserve(previewIndices.size());
    for (int previewIndex : previewIndices)
    {
        results.push_back(generateThumbnail(mediaFile, maxWidth, maxHeight, previewIndex));
    }
    return results;
}
