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

#include "FFFRUtility.h"
#include "FFFrameReader.h"

#include <algorithm>

extern "C" {
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

string getPresetString(const EncoderOptions::Preset preset)
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

Encoder::OutputFormatContextPtr::OutputFormatContextPtr(AVFormatContext* formatContext) noexcept
    : m_formatContext(formatContext, [](AVFormatContext* p) { avformat_free_context(p); })
{}

AVFormatContext* Encoder::OutputFormatContextPtr::get() const noexcept
{
    return m_formatContext.get();
}

AVFormatContext* Encoder::OutputFormatContextPtr::operator->() const noexcept
{
    return m_formatContext.get();
}

bool Encoder::encodeStream(
    const std::string& fileName, const std::shared_ptr<Stream>& stream, const EncoderOptions& options) noexcept
{
    // Create the new encoder
    const shared_ptr<Encoder> encoder =
        shared_ptr<Encoder>(new Encoder(fileName, stream, options.m_type, options.m_quality, options.m_preset));
    if (stream->m_codecContext.get() == nullptr) {
        // Stream creation failed
        return false;
    }
    return encoder->encodeStream();
}

Encoder::Encoder(const std::string& fileName, const std::shared_ptr<Stream>& stream, const EncodeType codecType,
    const uint8_t quality, const EncoderOptions::Preset preset) noexcept
{
    AVFormatContext* formatPtr = nullptr;
    auto ret = avformat_alloc_output_context2(&formatPtr, nullptr, nullptr, fileName.c_str());
    OutputFormatContextPtr tempFormat(formatPtr);
    if (ret < 0) {
        log("Failed to open output stream "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }
    const auto outStream = avformat_new_stream(tempFormat.get(), nullptr);
    if (outStream == nullptr) {
        log("Failed to create an output stream", LogLevel::Error);
        return;
    }

    // Find the required encoder
    const auto encoder = avcodec_find_encoder(getCodecID(codecType));
    if (!encoder) {
        log("Requested encoder is not supported", LogLevel::Error);
        return;
    }
    CodecContextPtr tempCodec(avcodec_alloc_context3(encoder));
    if (tempCodec.get() == nullptr) {
        log("Failed allocating encoder context", LogLevel::Error);
        return;
    }

    // Setup encoding parameters
    tempCodec->height = stream->getHeight();
    tempCodec->width = stream->getWidth();
    tempCodec->sample_aspect_ratio = stream->m_codecContext->sample_aspect_ratio;
    tempCodec->pix_fmt = getPixelFormat(stream->getPixelFormat());
    tempCodec->time_base = stream->m_formatContext->streams[stream->m_index]->time_base;
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

    // Open the encoder
    ret = avcodec_open2(tempCodec.get(), encoder, &opts);
    if (ret < 0) {
        log("Failed opening video encoder: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }
    ret = avcodec_parameters_from_context(outStream->codecpar, tempCodec.get());
    if (ret < 0) {
        log("Failed copying parameters to encoder context: "s += getFfmpegErrorString(ret), LogLevel::Error);
    }

    // Set the output stream timebase
    outStream->time_base = tempCodec.get()->time_base;

    // Open output file if required
    if (!(tempFormat->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&tempFormat->pb, fileName.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            log(("Failed to open output file: "s += fileName) += ", "s += getFfmpegErrorString(ret), LogLevel::Error);
            return;
        }
    }

    // Init the muxer and write out file header
    ret = avformat_write_header(tempFormat.get(), nullptr);
    if (ret < 0) {
        log(("Failed writing header to output file: "s += fileName) += ", "s += getFfmpegErrorString(ret),
            LogLevel::Error);
        return;
    }

    // Make the new encoder
    m_formatContext = move(tempFormat);
    m_codecContext = move(tempCodec);
    m_stream = stream;
}

bool Encoder::encodeStream() const noexcept
{
    while (true) {
        // Get next frame
        auto err = m_stream->getNextFrame();
        if (err.index() == 0) {
            try {
                if (get<0>(err) == false) {
                    return false;
                }
            } catch (...) {
                // Should never get here
                return false;
            }
            // Send a flush frame
            auto ret = avcodec_send_frame(m_codecContext.get(), nullptr);
            if (ret < 0) {
                log("Failed to send flush packet to encoder: "s += getFfmpegErrorString(ret), LogLevel::Error);
                return false;
            }
            if (!encodeFrames()) {
                return false;
            }
            ret = av_write_trailer(m_formatContext.get());
            if (ret < 0) {
                log("Failed to write file trailer: "s += getFfmpegErrorString(ret), LogLevel::Error);
                return false;
            }
            return true;
        }
        try {
            auto frame = get<1>(err);
            const auto ret = avcodec_send_frame(m_codecContext.get(), *frame->m_frame);
            if (ret < 0) {
                log("Failed to send packet to encoder: "s += getFfmpegErrorString(ret), LogLevel::Error);
                return false;
            }
            if (!encodeFrames()) {
                return false;
            }
        } catch (...) {
            // Should never get here
            return false;
        }
    }
}

bool Encoder::encodeFrames() const noexcept
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
            log("Failed to receive encoded frame: "s += getFfmpegErrorString(ret), LogLevel::Error);
            return false;
        }

        // Setup packet for muxing
        packet.stream_index = 0;
        av_packet_rescale_ts(&packet, m_codecContext->time_base, m_formatContext->streams[0]->time_base);

        // Mux encoded frame
        ret = av_interleaved_write_frame(m_formatContext.get(), &packet);
        if (ret < 0) {
            av_packet_unref(&packet);
            log("Failed to write encoded frame: "s += getFfmpegErrorString(ret), LogLevel::Error);
            return false;
        }

        av_packet_unref(&packet);
    }
    return true;
}
} // namespace Ffr