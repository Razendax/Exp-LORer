#include <gtest/gtest.h>

#include "MediaProcessingUseCase.h"
#include "MockMediaDecoder.h"

using ::testing::Return;
using ::testing::_;

TEST(MediaProcessingUseCase, GenerateThumbnailRejectsNonPositiveDimensions)
{
    MockMediaDecoder decoder;
    EXPECT_CALL(decoder, generateThumbnail(_, _, _)).Times(0);

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 0, 100);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(MediaProcessingUseCase, GenerateThumbnailDelegatesToDecoder)
{
    MockMediaDecoder decoder;
    MediaThumbnail thumbnail;
    thumbnail.bytes = { std::byte{ 0xFF }, std::byte{ 0xD8 } };
    thumbnail.metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(1000), "h264").value();
    EXPECT_CALL(decoder, generateThumbnail(std::filesystem::path("C:/media/clip.mp4"), 128, 128))
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 128, 128);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().bytes.size(), 2u);
    EXPECT_EQ(result.value().metadata.width(), 1920);
}
