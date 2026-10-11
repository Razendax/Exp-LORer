#pragma once

#include <gmock/gmock.h>

#include "IMediaDecoder.h"

class MockMediaDecoder : public IMediaDecoder
{
public:
    MOCK_METHOD(Result<MediaThumbnail>, generateThumbnail,
                (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight, int previewIndex), (const, override));
    MOCK_METHOD(std::vector<Result<MediaThumbnail>>, generateThumbnails,
                (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight, const std::vector<int>& previewIndices),
                (const, override));
};
