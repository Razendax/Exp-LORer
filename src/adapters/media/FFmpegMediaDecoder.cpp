#include "FFmpegMediaDecoder.h"

#include <algorithm>
#include <cstring>

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
    // Reads video width/height/codec name for the first decodable video stream. Shared by
    // extractMetadata() and generateThumbnail() so the open/find-stream boilerplate isn't
    // duplicated.
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
}

Result<MediaMetadata> FFmpegMediaDecoder::extractMetadata(const std::filesystem::path& mediaFile) const
{
    const std::string url = PathUtf8::toUtf8(WindowsLongPath::withPrefix(mediaFile));

    OpenedVideo opened;
    if (auto openResult = openVideo(url, opened); !openResult)
    {
        return Result<MediaMetadata>::failure(std::move(openResult).error());
    }

    const AVCodecParameters* codecpar = opened.formatContext->streams[opened.videoStreamIndex]->codecpar;
    const int width = codecpar->width;
    const int height = codecpar->height;

    std::chrono::milliseconds duration{ 0 };
    if (opened.formatContext->duration > 0)
    {
        duration = std::chrono::milliseconds(opened.formatContext->duration / (AV_TIME_BASE / 1000));
    }

    const std::string codecName = opened.decoder != nullptr ? opened.decoder->name : "unknown";

    return MediaMetadata::create(width, height, duration, codecName);
}

Result<std::vector<std::byte>> FFmpegMediaDecoder::generateThumbnail(const std::filesystem::path& mediaFile, int maxWidth,
                                                                       int maxHeight) const
{
    const std::string url = PathUtf8::toUtf8(WindowsLongPath::withPrefix(mediaFile));

    OpenedVideo opened;
    if (auto openResult = openVideo(url, opened); !openResult)
    {
        return Result<std::vector<std::byte>>::failure(std::move(openResult).error());
    }
    if (auto decoderResult = openDecoder(opened, url); !decoderResult)
    {
        return Result<std::vector<std::byte>>::failure(std::move(decoderResult).error());
    }

    // Seek to ~10% into the stream for a representative frame (Architecture.md §14.25); falls
    // back to the first decodable frame when duration is unavailable.
    if (opened.formatContext->duration > 0)
    {
        const int64_t seekTarget = opened.formatContext->duration / 10;
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
        return Result<std::vector<std::byte>>::failure(Error(ErrorCode::IoError, "FFmpeg failed to decode a frame from " + url));
    }

    const double widthScale = static_cast<double>(maxWidth) / decodedFrame->width;
    const double heightScale = static_cast<double>(maxHeight) / decodedFrame->height;
    const double scale = std::min({ widthScale, heightScale, 1.0 });
    const int targetWidth = std::max(1, static_cast<int>(decodedFrame->width * scale));
    const int targetHeight = std::max(1, static_cast<int>(decodedFrame->height * scale));

    SwsContext* swsContext = sws_getContext(decodedFrame->width, decodedFrame->height,
                                             static_cast<AVPixelFormat>(decodedFrame->format), targetWidth, targetHeight,
                                             AV_PIX_FMT_YUVJ420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (swsContext == nullptr)
    {
        av_frame_free(&decodedFrame);
        return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to create a scaling context for " + url));
    }

    AVFrame* scaledFrame = av_frame_alloc();
    scaledFrame->format = AV_PIX_FMT_YUVJ420P;
    scaledFrame->width = targetWidth;
    scaledFrame->height = targetHeight;
    if (av_frame_get_buffer(scaledFrame, 0) < 0)
    {
        sws_freeContext(swsContext);
        av_frame_free(&scaledFrame);
        av_frame_free(&decodedFrame);
        return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to allocate the scaled frame for " + url));
    }

    sws_scale(swsContext, decodedFrame->data, decodedFrame->linesize, 0, decodedFrame->height, scaledFrame->data,
              scaledFrame->linesize);
    sws_freeContext(swsContext);
    av_frame_free(&decodedFrame);

    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (encoder == nullptr)
    {
        av_frame_free(&scaledFrame);
        return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "MJPEG encoder not available"));
    }

    AVCodecContext* encoderContext = avcodec_alloc_context3(encoder);
    if (encoderContext == nullptr)
    {
        av_frame_free(&scaledFrame);
        return Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to allocate the MJPEG encoder context for " + url));
    }
    encoderContext->width = targetWidth;
    encoderContext->height = targetHeight;
    encoderContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
    encoderContext->time_base = AVRational{ 1, 25 };

    Result<std::vector<std::byte>> result = Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "unreachable"));

    if (avcodec_open2(encoderContext, encoder, nullptr) < 0)
    {
        result = Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to open the MJPEG encoder for " + url));
    }
    else if (avcodec_send_frame(encoderContext, scaledFrame) < 0)
    {
        result =
            Result<std::vector<std::byte>>::failure(Error(ErrorCode::Unknown, "Failed to send a frame to the MJPEG encoder for " + url));
    }
    else
    {
        AVPacket* encodedPacket = av_packet_alloc();
        if (avcodec_receive_packet(encoderContext, encodedPacket) < 0)
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

    avcodec_free_context(&encoderContext);
    av_frame_free(&scaledFrame);

    return result;
}
