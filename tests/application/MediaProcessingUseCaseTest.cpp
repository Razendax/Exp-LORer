#include <gtest/gtest.h>

#include "MediaProcessingUseCase.h"
#include "MockMediaDecoder.h"

using ::testing::Return;
using ::testing::_;

TEST(MediaProcessingUseCase, ExtractMetadataDelegatesToDecoder)
{
    MockMediaDecoder decoder;
    MediaMetadata metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(1000), "h264").value();
    EXPECT_CALL(decoder, extractMetadata(std::filesystem::path("C:/media/clip.mp4")))
        .WillOnce(Return(Result<MediaMetadata>::success(metadata)));

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.extractMetadata("C:/media/clip.mp4");

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().width(), 1920);
}

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
    std::vector<std::byte> bytes{ std::byte{ 0xFF }, std::byte{ 0xD8 } };
    EXPECT_CALL(decoder, generateThumbnail(std::filesystem::path("C:/media/clip.mp4"), 128, 128))
        .WillOnce(Return(Result<std::vector<std::byte>>::success(bytes)));

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 128, 128);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().size(), 2u);
}
