#include <gtest/gtest.h>

#include "MediaProcessingUseCase.h"
#include "MockMediaDecoder.h"

using ::testing::Return;
using ::testing::_;

TEST(MediaProcessingUseCase, GenerateThumbnailRejectsNonPositiveDimensions)
{
    MockMediaDecoder decoder;
    EXPECT_CALL(decoder, generateThumbnail(_, _, _, _)).Times(0);

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 0, 100);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(MediaProcessingUseCase, GenerateThumbnailRejectsOutOfRangePreviewIndex)
{
    MockMediaDecoder decoder;
    EXPECT_CALL(decoder, generateThumbnail(_, _, _, _)).Times(0);

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 100, 100, 5);

    ASSERT_TRUE(result.hasError());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(MediaProcessingUseCase, GenerateThumbnailDelegatesToDecoder)
{
    MockMediaDecoder decoder;
    MediaThumbnail thumbnail;
    thumbnail.bytes = { std::byte{ 0xFF }, std::byte{ 0xD8 } };
    thumbnail.metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(1000), "h264").value();
    EXPECT_CALL(decoder, generateThumbnail(std::filesystem::path("C:/media/clip.mp4"), 128, 128, 0))
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 128, 128);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().bytes.size(), 2u);
    EXPECT_EQ(result.value().metadata.width(), 1920);
}

TEST(MediaProcessingUseCase, GenerateThumbnailPassesThroughExplicitPreviewIndex)
{
    MockMediaDecoder decoder;
    MediaThumbnail thumbnail;
    thumbnail.bytes = { std::byte{ 0x01 } };
    thumbnail.metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(1000), "h264").value();
    EXPECT_CALL(decoder, generateThumbnail(std::filesystem::path("C:/media/clip.mp4"), 128, 128, 3))
        .WillOnce(Return(Result<MediaThumbnail>::success(thumbnail)));

    MediaProcessingUseCase useCase(decoder);
    auto result = useCase.generateThumbnail("C:/media/clip.mp4", 128, 128, 3);

    ASSERT_TRUE(result.hasValue());
}

TEST(MediaProcessingUseCase, GenerateThumbnailsRejectsNonPositiveDimensionsWithoutCallingDecoder)
{
    MockMediaDecoder decoder;
    EXPECT_CALL(decoder, generateThumbnails(_, _, _, _)).Times(0);

    MediaProcessingUseCase useCase(decoder);
    auto results = useCase.generateThumbnails("C:/media/clip.mp4", 0, 100, { 0, 1 });

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].hasError());
    EXPECT_EQ(results[0].error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(results[1].hasError());
    EXPECT_EQ(results[1].error().code, ErrorCode::InvalidArgument);
}

TEST(MediaProcessingUseCase, GenerateThumbnailsRejectsBatchWithAnyOutOfRangePreviewIndex)
{
    MockMediaDecoder decoder;
    EXPECT_CALL(decoder, generateThumbnails(_, _, _, _)).Times(0);

    MediaProcessingUseCase useCase(decoder);
    auto results = useCase.generateThumbnails("C:/media/clip.mp4", 100, 100, { 0, 5 });

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].hasError());
    EXPECT_EQ(results[0].error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(results[1].hasError());
    EXPECT_EQ(results[1].error().code, ErrorCode::InvalidArgument);
}

TEST(MediaProcessingUseCase, GenerateThumbnailsDelegatesToDecoder)
{
    MockMediaDecoder decoder;
    MediaThumbnail thumbnail;
    thumbnail.bytes = { std::byte{ 0xFF }, std::byte{ 0xD8 } };
    thumbnail.metadata = MediaMetadata::create(1920, 1080, std::chrono::milliseconds(1000), "h264").value();
    EXPECT_CALL(decoder, generateThumbnails(std::filesystem::path("C:/media/clip.mp4"), 128, 128, std::vector<int>{ 0, 1, 2 }))
        .WillOnce(Return(std::vector<Result<MediaThumbnail>>(3, Result<MediaThumbnail>::success(thumbnail))));

    MediaProcessingUseCase useCase(decoder);
    auto results = useCase.generateThumbnails("C:/media/clip.mp4", 128, 128, { 0, 1, 2 });

    ASSERT_EQ(results.size(), 3u);
    for (const auto& result : results)
    {
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(result.value().bytes.size(), 2u);
    }
}
