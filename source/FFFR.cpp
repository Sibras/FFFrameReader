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
#include "FFFRDecoderContext.h"
#include "FFFrameReader.h"

#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/log.h>
}

using namespace std;

namespace Ffr {
static enum AVPixelFormat getHardwareFormatNvdec(AVCodecContext* context, const enum AVPixelFormat* pixelFormats)
{
    enum AVPixelFormat pixelFormat;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(context->codec, i);
        if (!config) {
            av_log(nullptr, AV_LOG_ERROR, "Decoder does not support device type: %s, %s\n", context->codec->name,
                av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_CUDA));
            return AV_PIX_FMT_NONE;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == AV_HWDEVICE_TYPE_CUDA) {
            pixelFormat = config->pix_fmt;
            break;
        }
    }
    for (const enum AVPixelFormat* p = pixelFormats; *p != -1; p++) {
        if (*p == pixelFormat) {
            return *p;
        }
    }
    av_log(nullptr, AV_LOG_ERROR, "Failed to get hardware surface format\n");
    return AV_PIX_FMT_NONE;
}

FfFrameReader::DecoderOptions::DecoderOptions(const DecodeType type) noexcept
    : m_type(type)
{}

bool FfFrameReader::DecoderOptions::operator==(const DecoderOptions& other) const noexcept
{
    const bool ret = this->m_type == other.m_type && this->m_bufferLength == other.m_bufferLength &&
        this->m_device == other.m_device;
    if (!ret) {
        return ret;
    }
    if (this->m_type == DecodeType::Software) {
        return true;
    }
    try {
        if (this->m_type == DecodeType::CUDA) {
            return any_cast<CUcontext>(this->m_context) == any_cast<CUcontext>(other.m_context);
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool FfFrameReader::DecoderOptions::operator!=(const DecoderOptions& other) const noexcept
{
    return !(*this == other);
}

bool FfFrameReader::DecoderOptions::operator<(const DecoderOptions& other) const noexcept
{
    if (this->m_type < other.m_type) {
        return true;
    }
    if (other.m_type < this->m_type) {
        return false;
    }
    if (this->m_bufferLength < other.m_bufferLength) {
        return true;
    }
    if (other.m_bufferLength < this->m_bufferLength) {
        return false;
    }
    try {
        if (this->m_type == DecodeType::CUDA) {
            if (any_cast<CUcontext>(this->m_context) < any_cast<CUcontext>(other.m_context)) {
                return true;
            }
            if (any_cast<CUcontext>(other.m_context) < any_cast<CUcontext>(this->m_context)) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return this->m_device < other.m_device;
}

FfFrameReader::FfFrameReader()
{
    setLogLevel(LogLevel::Error);
}

variant<bool, shared_ptr<Stream>> FfFrameReader::getStream(const string& filename, const DecoderOptions& options) const
    noexcept
{
    // TODO: Allow specifying a filter chain of color conversion, scale and cropping
    AVFormatContext* formatPtr = nullptr;
    auto ret = avformat_open_input(&formatPtr, filename.c_str(), nullptr, nullptr);
    Stream::FormatContextPtr tempFormat(formatPtr);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed to open input stream '%s': %s\n", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }
    ret = avformat_find_stream_info(tempFormat.get(), nullptr);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed finding stream information '%s': %s\n", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Get the primary video stream
    AVCodec* decoder = nullptr;
    ret = av_find_best_stream(tempFormat.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed to find video stream in file '%s': %s\n", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }
    AVStream* stream = tempFormat->streams[ret];
    int32_t index = ret;

    if (options.m_type != DecodeType::Software) {
        // Check if required codec is supported
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
            if (config == nullptr) {
                av_log(nullptr, AV_LOG_ERROR, "Decoder does not support device type: %s, %s\n", decoder->name,
                    av_hwdevice_get_type_name(DecoderContext::decodeTypeToFFmpeg(options.m_type)));
                return false;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_CUDA) {
                break;
            }
        }
    }

    // Create a decoder context
    Stream::CodecContextPtr tempCodec(avcodec_alloc_context3(decoder));
    if (tempCodec.get() == nullptr) {
        av_log(nullptr, AV_LOG_ERROR, "Failed allocating decoder context %s\n", filename.c_str());
        return false;
    }

    ret = avcodec_parameters_to_context(tempCodec.get(), stream->codecpar);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed copying parameters to decoder context '%s': %s\n", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Setup any required hardware decoding parameters
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "refcounted_frames", "1", 0);
    if (options.m_type != DecodeType::Software) {
        const auto deviceContext = make_shared<DecoderContext>(options);
        if (deviceContext->m_deviceContext.get() == nullptr) {
            // Device creation failed
            return false;
        }
        tempCodec->hw_device_ctx = av_buffer_ref(deviceContext->m_deviceContext.get());
        if (options.m_type == DecodeType::CUDA) {
            tempCodec->get_format = getHardwareFormatNvdec;
        } else {
            av_log(nullptr, AV_LOG_ERROR, "Hardware Device not properly implemented\n");
            return false;
        }
        // Enable extra hardware frames to ensure we don't run out of buffers
        const string buffers = to_string(options.m_bufferLength);
        av_dict_set(&opts, "extra_hw_frames", buffers.c_str(), 0);
    } else {
        av_dict_set(&opts, "threads", "auto", 0);
    }
    ret = avcodec_open2(tempCodec.get(), decoder, &opts);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed opening decoder for %s: %s\n", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Make a new stream object and return it
    const bool outputHost = options.m_outputHost && (options.m_type != DecodeType::Software);
    return make_shared<Stream>(tempFormat, index, tempCodec, options.m_bufferLength, outputHost);
}

void FfFrameReader::setLogLevel(const LogLevel level)
{
    av_log_set_level(static_cast<int>(level));
}
} // namespace Ffr