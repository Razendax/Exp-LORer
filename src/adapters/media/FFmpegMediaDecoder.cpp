#include "FFmpegMediaDecoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "Logging.h"
#include "MediaPreviewFrames.h"
#include "PathUtf8.h"
#include "WindowsLongPath.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace
{
    // Reads video width/height/codec name for the first decodable video stream, kept open so the
    // single generateThumbnail() call below can build MediaMetadata from it directly rather than
    // probing the file a second time (Architecture.md §14.63).
    struct OpenedVideo
    {
        AVFormatContext* formatContext = nullptr;
        AVCodecContext* decoderContext = nullptr;
        int videoStreamIndex = -1;
        const AVCodec* decoder = nullptr;

        ~OpenedVideo()
        {
            if (decoderContext != nullptr)
            {
                avcodec_free_context(&decoderContext);
            }
            if (formatContext != nullptr)
            {
                avformat_close_input(&formatContext);
            }
        }
    };

    Result<void> openVideo(const std::string& url, OpenedVideo& opened)
    {
        if (avformat_open_input(&opened.formatContext, url.c_str(), nullptr, nullptr) != 0)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "FFmpeg failed to open " + url));
        }

        if (avformat_find_stream_info(opened.formatContext, nullptr) < 0)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "FFmpeg failed to read stream info for " + url));
        }

        opened.videoStreamIndex =
            av_find_best_stream(opened.formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &opened.decoder, 0);
        if (opened.videoStreamIndex < 0 || opened.decoder == nullptr)
        {
            return Result<void>::failure(Error(ErrorCode::NotFound, "No video stream found in " + url));
        }

        return Result<void>::success();
    }

    Result<void> openDecoder(OpenedVideo& opened, const std::string& url)
    {
        opened.decoderContext = avcodec_alloc_context3(opened.decoder);
        if (opened.decoderContext == nullptr)
        {
            return Result<void>::failure(Error(ErrorCode::Unknown, "Failed to allocate decoder context for " + url));
        }

        avcodec_parameters_to_context(opened.decoderContext, opened.formatContext->streams[opened.videoStreamIndex]->codecpar);

        if (avcodec_open2(opened.decoderContext, opened.decoder, nullptr) < 0)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "FFmpeg failed to open decoder for " + url));
        }

        return Result<void>::success();
    }

    // Seeks to one of MediaPreviewFrames::kCount equally-spaced positions into an already-open
    // session and decodes one frame from it (Architecture.md §14.25/§14.64); falls back to the
    // first decodable frame when duration is unavailable. previewIndex 0 reproduces the original
    // ~10% position. Caller owns the returned AVFrame (av_frame_free).
    Result<AVFrame*> decodeFrameAt(OpenedVideo& opened, int previewIndex, const std::string& url)
    {
        if (opened.formatContext->duration > 0)
        {
            const int clampedPreviewIndex = std::clamp(previewIndex, 0, MediaPreviewFrames::kCount - 1);
            const int64_t seekTarget = static_cast<int64_t>(
                static_cast<double>(opened.formatContext->duration) * MediaPreviewFrames::fractionForIndex(clampedPreviewIndex));
            av_seek_frame(opened.formatContext, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(opened.decoderContext);
        }

        AVPacket* packet = av_packet_alloc();
        AVFrame* decodedFrame = av_frame_alloc();

        bool gotFrame = false;
        while (!gotFrame && av_read_frame(opened.formatContext, packet) >= 0)
        {
            if (packet->stream_index == opened.videoStreamIndex && avcodec_send_packet(opened.decoderContext, packet) == 0)
            {
                gotFrame = avcodec_receive_frame(opened.decoderContext, decodedFrame) == 0;
            }
            av_packet_unref(packet);
        }
        if (!gotFrame)
        {
            // Drain: the last packet sent may need one more receive call to flush a buffered frame.
            avcodec_send_packet(opened.decoderContext, nullptr);
            gotFrame = avcodec_receive_frame(opened.decoderContext, decodedFrame) == 0;
        }
        av_packet_free(&packet);

        if (!gotFrame)
        {
            av_frame_free(&decodedFrame);
            return Result<AVFrame*>::failure(Error(ErrorCode::IoError, "FFmpeg failed to decode a frame from " + url));
        }

        return Result<AVFrame*>::success(decodedFrame);
    }

    // sws/MJPEG-encoder context shared across every frame decoded within one generateThumbnails()
    // batch (Architecture.md §14.77) -- target dimensions are constant for a given call (same
    // source video, same maxWidth/maxHeight), so creating these once on the first successfully
    // decoded frame and reusing them for the rest avoids a redundant context alloc per frame.
    struct SharedEncodeContext
    {
        SwsContext* swsContext = nullptr;
        AVCodecContext* encoderContext = nullptr;
        int targetWidth = 0;
        int targetHeight = 0;
        std::int64_t nextPts = 0;

        ~SharedEncodeContext()
        {
            if (encoderContext != nullptr)
            {
                avcodec_free_context(&encoderContext);
            }
            if (swsContext != nullptr)
            {
                sws_freeContext(swsContext);
            }
        }
    };

    // Scales `decodedFrame` (not freed here -- caller's responsibility) via `shared`'s
    // lazily-created SwsContext and JPEG-encodes it via `shared`'s lazily-created MJPEG encoder
    // context, creating either the first time this is called for a given `shared` instance.
    Result<std::vector<std::byte>> scaleAndEncode(const AVFrame* decodedFrame, int maxWidth, int maxHeight,
                                                     SharedEncodeContext& shared, const std::string& url)
    {
        if (shared.swsContext == nullptr)
        {
            const double widthScale = static_cast<double>(maxWidth) / decodedFrame->width;
            const double heightScale = static_cast<double>(maxHeight) / decodedFrame->height;
            const double scale = std::min({ widthScale, heightScale, 1.0 });
            shared.targetWidth = std::max(1, static_cast<int>(decodedFrame->width * scale));
            shared.targetHeight = std::max(1, static_cast<int>(decodedFrame->height * scale));

            shared.swsContext = sws_getContext(decodedFrame->width, decodedFrame->height,
                                                 static_cast<AVPixelFormat>(decodedFrame->format), shared.targetWidth,
                                                 shared.targetHeight, AV_PIX_FMT_YUVJ420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (shared.swsContext == nullptr)
            {
                return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to create a scaling context for " + url));
            }
        }

        AVFrame* scaledFrame = av_frame_alloc();
        scaledFrame->format = AV_PIX_FMT_YUVJ420P;
        scaledFrame->width = shared.targetWidth;
        scaledFrame->height = shared.targetHeight;
        if (av_frame_get_buffer(scaledFrame, 0) < 0)
        {
            av_frame_free(&scaledFrame);
            return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to allocate the scaled frame for " + url));
        }

        sws_scale(shared.swsContext, decodedFrame->data, decodedFrame->linesize, 0, decodedFrame->height, scaledFrame->data,
                   scaledFrame->linesize);

        if (shared.encoderContext == nullptr)
        {
            const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
            if (encoder == nullptr)
            {
                av_frame_free(&scaledFrame);
                return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "MJPEG encoder not available"));
            }

            shared.encoderContext = avcodec_alloc_context3(encoder);
            if (shared.encoderContext == nullptr)
            {
                av_frame_free(&scaledFrame);
                return Result<std::vector<std::byte>>::failure(
                    Error(ErrorCode::Unknown, "Failed to allocate the MJPEG encoder context for " + url));
            }
            shared.encoderContext->width = shared.targetWidth;
            shared.encoderContext->height = shared.targetHeight;
            shared.encoderContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
            shared.encoderContext->time_base = AVRational{ 1, 25 };

            if (avcodec_open2(shared.encoderContext, encoder, nullptr) < 0)
            {
                av_frame_free(&scaledFrame);
                avcodec_free_context(&shared.encoderContext);
                return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to open the MJPEG encoder for " + url));
            }
        }

        scaledFrame->pts = shared.nextPts++;

        Result<std::vector<std::byte>> result = Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "unreachable"));

        if (avcodec_send_frame(shared.encoderContext, scaledFrame) < 0)
        {
            result =
                Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to send a frame to the MJPEG encoder for " + url));
        }
        else
        {
            AVPacket* encodedPacket = av_packet_alloc();
            if (avcodec_receive_packet(shared.encoderContext, encodedPacket) < 0)
            {
                result = Result<std::vector<std::byte>>::failure(
                    Error(ErrorCode::Unknown, "MJPEG encoder produced no packet for " + url));
            }
            else
            {
                std::vector<std::byte> encoded(static_cast<std::size_t>(encodedPacket->size));
                std::memcpy(encoded.data(), encodedPacket->data, encoded.size());
                result = Result<std::vector<std::byte>>::success(std::move(encoded));
            }
            av_packet_free(&encodedPacket);
        }

        av_frame_free(&scaledFrame);
        return result;
    }
}

Result<MediaThumbnail> FFmpegMediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                                int maxHeight, int previewIndex) const
{
    return generateThumbnails(mediaFile, maxWidth, maxHeight, { previewIndex })[0];
}

std::vector<Result<MediaThumbnail>> FFmpegMediaDecoder::generateThumbnails(const std::filesystem::path& mediaFile, int maxWidth,
                                                                              int maxHeight,
                                                                              const std::vector<int>& previewIndices) const
{
    const std::string url = PathUtf8::toUtf8(WindowsLongPath::withPrefix(mediaFile));

    OpenedVideo opened;
    if (auto openResult = openVideo(url, opened); !openResult)
    {
        Logging::warn("FFmpegMediaDecoder::generateThumbnails failed: " + std::string(toLogString(openResult.error().code)));
        return std::vector<Result<MediaThumbnail>>(previewIndices.size(), Result<MediaThumbnail>::failure(openResult.error()));
    }
    if (auto decoderResult = openDecoder(opened, url); !decoderResult)
    {
        Logging::warn("FFmpegMediaDecoder::generateThumbnails failed: " + std::string(toLogString(decoderResult.error().code)));
        return std::vector<Result<MediaThumbnail>>(previewIndices.size(), Result<MediaThumbnail>::failure(decoderResult.error()));
    }

    // Built from the same already-open session used for every frame grab below (Architecture.md
    // §14.63), rather than a second avformat_open_input/avformat_find_stream_info probe of the
    // same file -- falls back to zero-valued metadata (graceful degradation) if these fields
    // somehow don't form a valid MediaMetadata; the thumbnails below are still useful on their own.
    MediaMetadata metadata;
    {
        const AVCodecParameters* codecpar = opened.formatContext->streams[opened.videoStreamIndex]->codecpar;

        std::chrono::milliseconds duration{ 0 };
        if (opened.formatContext->duration > 0)
        {
            duration = std::chrono::milliseconds(opened.formatContext->duration / (AV_TIME_BASE / 1000));
        }

        const std::string codecName = opened.decoder != nullptr ? opened.decoder->name : "unknown";
        const std::int64_t bitRateBps = std::max<std::int64_t>(0, opened.formatContext->bit_rate);

        if (auto created = MediaMetadata::create(codecpar->width, codecpar->height, duration, codecName, bitRateBps))
        {
            metadata = std::move(created).value();
        }
    }

    SharedEncodeContext shared;
    std::vector<Result<MediaThumbnail>> results;
    results.reserve(previewIndices.size());

    for (int previewIndex : previewIndices)
    {
        Result<AVFrame*> decoded = decodeFrameAt(opened, previewIndex, url);
        if (!decoded)
        {
            Logging::warn("FFmpegMediaDecoder::generateThumbnails failed: " + std::string(toLogString(decoded.error().code)));
            results.push_back(Result<MediaThumbnail>::failure(decoded.error()));
            continue;
        }

        AVFrame* decodedFrame = decoded.value();
        Result<std::vector<std::byte>> encoded = scaleAndEncode(decodedFrame, maxWidth, maxHeight, shared, url);
        av_frame_free(&decodedFrame);

        if (!encoded)
        {
            Logging::warn("FFmpegMediaDecoder::generateThumbnails failed: " + std::string(toLogString(encoded.error().code)));
            results.push_back(Result<MediaThumbnail>::failure(std::move(encoded).error()));
            continue;
        }

        Logging::debug("FFmpegMediaDecoder::generateThumbnails succeeded: " + std::to_string(shared.targetWidth) + "x" +
                        std::to_string(shared.targetHeight));
        results.push_back(Result<MediaThumbnail>::success(MediaThumbnail{ std::move(encoded).value(), metadata }));
    }

    return results;
}
