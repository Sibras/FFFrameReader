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

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
}
using namespace std;

static enum AVPixelFormat getHardwareFormatNvdec(AVCodecContext* context, const enum AVPixelFormat* pixelFormats)
{
    enum AVPixelFormat pixelFormat;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(context->codec, i);
        if (!config) {
            av_log(nullptr, AV_LOG_ERROR, "Decoder does not support device type: %s, %s", context->codec->name,
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
    av_log(nullptr, AV_LOG_ERROR, "Failed to get hardware surface format");
    return AV_PIX_FMT_NONE;
}

namespace FfFrameReader {
static enum AVHWDeviceType decodeTypeToFFmpeg(const DecoderContext::DecodeType type)
{
    if (type == DecoderContext::DecodeType::Nvdec) {
        return AV_HWDEVICE_TYPE_CUDA;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

DecoderContext::DecoderContext(const DecodeType type, const uint32_t bufferLength) noexcept
    : m_deviceType(type)
    , m_bufferLength(bufferLength)
{
    if (m_deviceType != DecodeType::Software) {
        // Create device specific options
        const string device = "0";
        // TODO: Allow specifying the device to use

        // Create the hardware context for decoding
        int err;
        if ((err = av_hwdevice_ctx_create(
                 &m_deviceContext, decodeTypeToFFmpeg(m_deviceType), device.c_str(), nullptr, 0)) < 0) {
            char buffer[AV_ERROR_MAX_STRING_SIZE];
            av_log(nullptr, AV_LOG_ERROR, "Failed to create specified hardware device: %s",
                av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, err));
            return;
            // TODO: need is Valid
        }
    }
}

DecoderContext::~DecoderContext() noexcept
{
    // Release the contexts
    if (m_deviceContext != nullptr) {
        av_buffer_unref(&m_deviceContext);
    }
}

variant<bool, shared_ptr<Stream>> DecoderContext::getStream(const string& filename) noexcept
{
    Stream::FormatContextPtr tempFormat;
    auto ret = avformat_open_input(&*tempFormat, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed to open input stream '%s': %s", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }
    ret = avformat_find_stream_info(*tempFormat, nullptr);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed finding stream information '%s': %s", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Get the primary video stream
    AVCodec* decoder = nullptr;
    ret = av_find_best_stream(*tempFormat, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed to find video stream in file '%s': %s", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }
    AVStream* stream = tempFormat->streams[ret];
    int32_t index = ret;

    if (m_deviceType != DecodeType::Software) {
        // Check if required codec is supported
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
            if (config == nullptr) {
                av_log(nullptr, AV_LOG_ERROR, "Decoder does not support device type: %s, %s", decoder->name,
                    av_hwdevice_get_type_name(decodeTypeToFFmpeg(m_deviceType)));
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
    if (*tempCodec == nullptr) {
        av_log(nullptr, AV_LOG_ERROR, "Failed allocating decoder context %s", filename.c_str());
        return false;
    }

    ret = avcodec_parameters_to_context(*tempCodec, stream->codecpar);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed copying parameters to decoder context '%s': %s", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Setup any required hardware decoding parameters
    if (m_deviceType != DecodeType::Software) {
        if (m_deviceType == DecodeType::Nvdec) {
            tempCodec->get_format = getHardwareFormatNvdec;
        } else {
            av_log(nullptr, AV_LOG_ERROR, "Hardware Device not properly implemented");
            return false;
        }
        tempCodec->hw_device_ctx = av_buffer_ref(m_deviceContext);
    }
    ret = avcodec_open2(*tempCodec, decoder, nullptr);
    if (ret < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE];
        av_log(nullptr, AV_LOG_ERROR, "Failed opening decoder for %s: %s", filename.c_str(),
            av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret));
        return false;
    }

    // Make a new stream object and add it to internal list
    return make_shared<Stream>(tempFormat, index, tempCodec, m_bufferLength);
}
} // namespace FfFrameReader