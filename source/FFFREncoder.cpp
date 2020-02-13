/**
 * Copyright 2019 Matthew Oliver
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "FFFREncoder.h"

#include "FFFRFilter.h"
#include "FFFRStreamUtils.h"
#include "FFFRUtility.h"
#include "FFFrameReader.h"

#include <algorithm>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

using namespace std;

namespace Ffr {
AVCodecID getCodecID(const EncodeType encoder) noexcept
{
    switch (encoder) {
        case EncodeType::h264: {
            return AV_CODEC_ID_H264;
        }
        case EncodeType::h265: {
            return AV_CODEC_ID_H265;
        }
        default: {
            return AV_CODEC_ID_NONE;
        }
    }
}

string getPresetString(const EncoderOptions::Preset preset) noexcept
{
    switch (preset) {
        case EncoderOptions::Preset::Ultrafast: {
            return "ultrafast";
        }
        case EncoderOptions::Preset::Superfast: {
            return "superfast";
        }
        case EncoderOptions::Preset::Veryfast: {
            return "veryfast";
        }
        case EncoderOptions::Preset::Faster: {
            return "faster";
        }
        case EncoderOptions::Preset::Fast: {
            return "fast";
        }
        case EncoderOptions::Preset::Medium: {
            return "medium";
        }
        case EncoderOptions::Preset::Slow: {
            return "slow";
        }
        case EncoderOptions::Preset::Slower: {
            return "slower";
        }
        case EncoderOptions::Preset::Veryslow: {
            return "veryslow";
        }
        case EncoderOptions::Preset::Placebo: {
            return "placebo";
        }
        default: {
            return "medium";
        }
    }
}

bool Encoder::encodeStream(
    const std::string& fileName, const std::shared_ptr<Stream>& stream, const EncoderOptions& options) noexcept
{
    // Create the new encoder
    const shared_ptr<Encoder> encoder = make_shared<Encoder>(fileName, stream->getWidth(), stream->getHeight(),
        getRational(StreamUtils::getSampleAspectRatio(stream.get())), stream->getPixelFormat(),
        getRational(StreamUtils::getFrameRate(stream.get())),
        stream->getDuration() -
            (stream->m_lastDecodedTimeStamp != INT64_MIN ? stream->timeStampToTime2(stream->m_lastDecodedTimeStamp) :
                                                           0),
        options.m_type, options.m_quality, options.m_preset, options.m_numThreads, options.m_gopSize,
        ConstructorLock());
    if (!encoder->isEncoderValid()) {
        // Encoder creation failed
        return false;
    }
    return encoder->encodeStream(stream);
}

Encoder::Encoder(const std::string& fileName, const uint32_t width, const uint32_t height, const Rational aspect,
    const PixelFormat format, const Rational frameRate, const int64_t duration, const EncodeType codecType,
    const uint8_t quality, const EncoderOptions::Preset preset, const uint32_t numThreads, const uint32_t gopSize,
    ConstructorLock) noexcept
{
    AVFormatContext* formatPtr = nullptr;
    auto ret = avformat_alloc_output_context2(&formatPtr, nullptr, nullptr, fileName.c_str());
    OutputFormatContextPtr tempFormat(formatPtr);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed to open output stream ", getFfmpegErrorString(ret));
        return;
    }
    const auto outStream = avformat_new_stream(tempFormat.get(), nullptr);
    if (outStream == nullptr) {
        logInternal(LogLevel::Error, "Failed to create an output stream");
        return;
    }

    // Find the required encoder
    const AVCodec* const encoder = avcodec_find_encoder(getCodecID(codecType));
    if (!encoder) {
        logInternal(LogLevel::Error, "Requested encoder is not supported");
        return;
    }
    CodecContextPtr tempCodec(avcodec_alloc_context3(encoder));
    if (tempCodec.get() == nullptr) {
        logInternal(LogLevel::Error, "Failed allocating encoder context");
        return;
    }

    // Setup encoding parameters
    tempCodec->height = height;
    tempCodec->width = width;
    tempCodec->sample_aspect_ratio = {aspect.m_numerator, aspect.m_denominator};
    tempCodec->pix_fmt = getPixelFormat(format);
    tempCodec->framerate = {frameRate.m_numerator, frameRate.m_denominator};
    tempCodec->time_base = av_inv_q(tempCodec->framerate);
    av_opt_set_int(tempCodec.get(), "refcounted_frames", 1, 0);

    if (tempFormat->oformat->flags & AVFMT_GLOBALHEADER) {
        tempCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Setup the desired encoding options
    AVDictionary* opts = nullptr;
    int32_t encoderCRF = 255 - quality;
    encoderCRF = encoderCRF / (255 / 51);
    // x264 allows crf from 0->51 where 23 is default
    // x265 allows crf from 0->51 where 28 is default and should correspond to 23 in x264
    // vp9 allows crf from 0->63 where 31 is default
    av_dict_set(&opts, "crf", to_string(encoderCRF).c_str(), 0);
    av_dict_set(&opts, "preset", getPresetString(preset).c_str(), 0);

    if (numThreads != 0) {
        av_dict_set(&opts, "threads", to_string(numThreads).c_str(), 0);
    }

    // Setup gop size
    if (gopSize != 0) {
        tempCodec->gop_size = gopSize;
        tempCodec->keyint_min = gopSize;
        string codecOpts;
        if (codecType == EncodeType::h264) {
            codecOpts = "x264opts";
        } else if (codecType == EncodeType::h265) {
            codecOpts = "x265-params";
        }
        if (!codecOpts.empty()) {
            const string keyInt = to_string(gopSize);
            const string settings = ("keyint="s += keyInt) += ":min-keyint="s += keyInt;
            av_dict_set(&opts, codecOpts.c_str(), settings.c_str(), 0);
        }
    }

    // Open the encoder
    ret = avcodec_open2(tempCodec.get(), encoder, &opts);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed opening video encoder: ", getFfmpegErrorString(ret));
        return;
    }
    ret = avcodec_parameters_from_context(outStream->codecpar, tempCodec.get());
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed copying parameters to encoder context: ", getFfmpegErrorString(ret));
        return;
    }

    // Set the output stream timebase
    outStream->time_base = tempCodec->time_base;
    outStream->r_frame_rate = tempCodec->framerate;
    outStream->avg_frame_rate = tempCodec->framerate;

    // Open output file if required
    if (!(tempFormat->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&tempFormat->pb, fileName.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            logInternal(LogLevel::Error, "Failed to open output file: ", fileName, ", ", getFfmpegErrorString(ret));
            return;
        }
    }

    // Init the muxer and write out file header
    ret = avformat_write_header(tempFormat.get(), nullptr);
    if (ret < 0) {
        logInternal(
            LogLevel::Error, "Failed writing header to output file: ", fileName, ", ", getFfmpegErrorString(ret));
        return;
    }

    // Give muxer hint about duration
    outStream->duration = av_rescale_q(duration, av_make_q(1, AV_TIME_BASE), outStream->time_base);
    tempFormat->duration = outStream->duration;

    // Make the new encoder
    m_formatContext = move(tempFormat);
    m_codecContext = move(tempCodec);
}

bool Encoder::isEncoderValid() const noexcept
{
    return (m_codecContext.get() != nullptr);
}

bool Encoder::encodeStream(const std::shared_ptr<Stream>& stream) const noexcept
{
    while (true) {
        // Get next frame
        auto frame = stream->getNextFrame();
        if (frame == nullptr) {
            if (!stream->isEndOfFile()) {
                return false;
            }
            return encodeFrame(frame, stream);
        }
        if (!encodeFrame(frame, stream)) {
            return false;
        }
    }
}

bool Encoder::encodeFrame(const std::shared_ptr<Frame>& frame, const std::shared_ptr<Stream>& stream) const noexcept
{
    if (frame != nullptr) {
        // Send frame to encoder
        frame->m_frame->best_effort_timestamp = av_rescale_q(
            frame->m_frame->best_effort_timestamp, stream->m_codecContext->time_base, m_codecContext->time_base);
        frame->m_frame->pts = frame->m_frame->best_effort_timestamp;
        const auto ret = avcodec_send_frame(m_codecContext.get(), *frame->m_frame);
        if (ret < 0) {
            logInternal(LogLevel::Error, "Failed to send packet to encoder: ", getFfmpegErrorString(ret));
            return false;
        }
        if (!muxFrames()) {
            return false;
        }
    } else {
        // Send a flush frame
        auto ret = avcodec_send_frame(m_codecContext.get(), nullptr);
        if (ret < 0) {
            logInternal(LogLevel::Error, "Failed to send flush packet to encoder: ", getFfmpegErrorString(ret));
            return false;
        }
        if (!muxFrames()) {
            return false;
        }
        av_interleaved_write_frame(m_formatContext.get(), nullptr);
        ret = av_write_trailer(m_formatContext.get());
        if (ret < 0) {
            logInternal(LogLevel::Error, "Failed to write file trailer: ", getFfmpegErrorString(ret));
            return false;
        }
    }
    return true;
}

bool Encoder::muxFrames() const noexcept
{
    // Get all encoder packets
    AVPacket packet;
    while (true) {
        packet.data = nullptr;
        packet.size = 0;
        av_init_packet(&packet);
        auto ret = avcodec_receive_packet(m_codecContext.get(), &packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(&packet);
            break;
        } else if (ret < 0) {
            av_packet_unref(&packet);
            logInternal(LogLevel::Error, "Failed to receive encoded frame: ", getFfmpegErrorString(ret));
            return false;
        }

        // Setup packet for muxing
        packet.stream_index = 0;
        packet.duration = av_rescale_q(1, av_inv_q(m_codecContext->framerate), m_codecContext->time_base);
        av_packet_rescale_ts(&packet, m_codecContext->time_base, m_formatContext->streams[0]->time_base);
        packet.pos = -1;

        // Mux encoded frame
        ret = av_interleaved_write_frame(m_formatContext.get(), &packet);
        if (ret < 0) {
            av_packet_unref(&packet);
            logInternal(LogLevel::Error, "Failed to write encoded frame: ", getFfmpegErrorString(ret));
            return false;
        }

        av_packet_unref(&packet);
    }
    return true;
}
} // namespace Ffr
