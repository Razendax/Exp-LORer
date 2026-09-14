#pragma once

#include <gmock/gmock.h>

#include "IMediaDecoder.h"

class MockMediaDecoder : public IMediaDecoder
{
public:
    MOCK_METHOD(Result<MediaMetadata>, extractMetadata, (const std::filesystem::path& mediaFile), (const, override));
    MOCK_METHOD(Result<std::vector<std::byte>>, generateThumbnail, (const std::filesystem::path& mediaFile, int maxWidth, int maxHeight), (const, override));
};
