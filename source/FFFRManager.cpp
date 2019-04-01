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
#include "FFFRManager.h"

#include "FfFrameReader.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
using namespace std;

static enum AVPixelFormat getHardwareFormatNvdec(AVCodecContext* context, const enum AVPixelFormat* pixelFormats)
{
    enum AVPixelFormat pixelFormat;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(context->codec, i);
        if (!config) {
            FfFrameReader::Interface::logError("Decoder does not support device type: "s + context->codec->name +
                ", "s + av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_CUDA));
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
    FfFrameReader::Interface::logError("Failed to get hardware surface format");
    return AV_PIX_FMT_NONE;
}

namespace FfFrameReader {
Manager::Manager(const DecodeType type, const uint32_t bufferLength) noexcept
    : m_deviceType(static_cast<AVHWDeviceType>(type))
    , m_bufferLength(bufferLength)
{
    if (m_deviceType != AV_HWDEVICE_TYPE_NONE) {
        // Create device specific options
        const string device = "0";
        // TODO: Allow specifying the device to use

        // Create the hardware context for decoding
        int err;
        if ((err = av_hwdevice_ctx_create(&m_deviceContext, m_deviceType, device.c_str(), nullptr, 0)) < 0) {
            Interface::logError("Failed to create specified hardware device: " + Log::getFfmpegErrorString(err));
            return;
            // TODO: need is Valid
        }
    }
}

Manager::~Manager() noexcept
{
    // Release the contexts
    if (m_deviceContext != nullptr) {
        av_buffer_unref(&m_deviceContext);
    }
}

variant<bool, shared_ptr<Stream>> Manager::getStream(const string& filename) noexcept
{
    // Check if the stream already exists
    const auto found = m_streams.find(filename);
    if (found != m_streams.end()) {
        return found->second;
    }

    Stream::FormatContextPtr tempFormat;
    auto ret = avformat_open_input(&*tempFormat, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        Interface::logError("Failed to open input stream " + filename + ": " + Log::getFfmpegErrorString(ret));
        return false;
    }
    ret = avformat_find_stream_info(*tempFormat, nullptr);
    if (ret < 0) {
        Interface::logError("Failed finding stream information " + filename + ": " + Log::getFfmpegErrorString(ret));
        return false;
    }

    // Get the primary video stream
    AVCodec* decoder = nullptr;
    ret = av_find_best_stream(*tempFormat, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        Interface::logError("Failed to find video stream in file " + filename + ": " + Log::getFfmpegErrorString(ret));
        return false;
    }
    AVStream* stream = tempFormat->streams[ret];
    int32_t index = ret;

    // Create a decoder context
    Stream::CodecContextPtr tempCodec(avcodec_alloc_context3(decoder));
    if (*tempCodec == nullptr) {
        Interface::logError("Failed allocating decoder context " + filename);
        return false;
    }

    ret = avcodec_parameters_to_context(*tempCodec, stream->codecpar);
    if (ret < 0) {
        Interface::logError(
            "Failed copying parameters to decoder context " + filename + ": " + Log::getFfmpegErrorString(ret));
        return false;
    }

    // Setup any required hardware decoding parameters
    if (m_deviceType != AV_HWDEVICE_TYPE_NONE) {
        tempCodec->get_format = getHardwareFormatNvdec;
        tempCodec->hw_device_ctx = av_buffer_ref(m_deviceContext);
    }
    ret = avcodec_open2(*tempCodec, decoder, nullptr);
    if (ret < 0) {
        Interface::logError("Failed opening decoder for " + filename + ": " + Log::getFfmpegErrorString(ret));
        return false;
    }

    // Make a new stream object and add it to internal list
    m_streams[filename] = make_shared<Stream>(tempFormat, index, tempCodec, m_bufferLength);
    return m_streams[filename];
}

void Manager::releaseStream(const string& filename) noexcept
{
    // Check if stream exists in the list
    if (const auto found = m_streams.find(filename); found != m_streams.end()) {
        m_streams.erase(found);
    }
}
} // namespace FfFrameReader