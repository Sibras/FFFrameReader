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

#include "FFFRUtility.h"

#include <any>
#include <string>
#include <utility>

#define CUDA_VERSION PreventNeedingCudaSDK
using CUcontext = struct CUctx_st*;
using CUstream = struct CUstream_st*;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>
}

using namespace std;

namespace Ffr {
static enum AVPixelFormat getHardwareFormatNvdec(AVCodecContext* context, const enum AVPixelFormat* pixelFormats)
{
    enum AVPixelFormat pixelFormat;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* const config = avcodec_get_hw_config(context->codec, i);
        if (!config) {
            logInternal(LogLevel::Error, "Decoder does not support device type: ", context->codec->name, ", "s,
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
    logInternal(LogLevel::Error, "Failed to get hardware surface format");
    return AV_PIX_FMT_NONE;
}

enum AVHWDeviceType DecoderContext::DecodeTypeToFFmpeg(const DecodeType type) noexcept
{
    if (type == DecodeType::Cuda) {
        return AV_HWDEVICE_TYPE_CUDA;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

DecoderContext::DeviceContextPtr::DeviceContextPtr(AVBufferRef* deviceContext) noexcept
    : m_deviceContext(deviceContext, [](AVBufferRef* p) noexcept { av_buffer_unref(&p); })
{}

AVBufferRef* DecoderContext::DeviceContextPtr::get() const noexcept
{
    return m_deviceContext.get();
}

AVBufferRef* DecoderContext::DeviceContextPtr::operator->() const noexcept
{
    return m_deviceContext.get();
}

DecoderContext::FormatFunction DecoderContext::getFormatFunction() const noexcept
{
    if (m_deviceContext.get() != nullptr) {
        auto* const deviceContext = reinterpret_cast<AVHWDeviceContext*>(m_deviceContext->data);
        if (deviceContext->type == AV_HWDEVICE_TYPE_CUDA) {
            return getHardwareFormatNvdec;
        }
    }
    return nullptr;
}

DecoderContext::DecodeType DecoderContext::getType() const noexcept
{
    if (m_deviceContext.get() != nullptr) {
        auto* const deviceContext = reinterpret_cast<AVHWDeviceContext*>(m_deviceContext->data);
        if (deviceContext->type == AV_HWDEVICE_TYPE_CUDA) {
            return DecodeType::Cuda;
        }
    }
    return DecodeType::Software;
}

DecoderContext::DecoderContext(const DecodeType type, const std::any& context, const uint32_t device) noexcept
{
    if (type != DecodeType::Software) {
        // Create device specific options
        string deviceName;
        try {
            deviceName = to_string(device);
        } catch (...) {
        }
        const auto typeInternal = DecodeTypeToFFmpeg(type);

        // Check if we should create a custom hardware device
        if (context.has_value()) {
            DeviceContextPtr tempDevice(av_hwdevice_ctx_alloc(typeInternal));
            if (tempDevice.get() == nullptr) {
                logInternal(LogLevel::Error, "Failed to create custom hardware device");
                return;
            }
            auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(tempDevice->data);
            deviceContext->free = nullptr;
            if (typeInternal == AV_HWDEVICE_TYPE_CUDA) {
                if (context.type() != typeid(CUcontext)) {
                    logInternal(LogLevel::Error, "Specified device context does not match the required type");
                    return;
                }
                auto* cudaDevice = static_cast<AVCUDADeviceContext*>(deviceContext->hwctx);
                try {
                    cudaDevice->cuda_ctx = any_cast<CUcontext>(context);
                } catch (...) {
                }
                cudaDevice->stream = nullptr;
            }
            const auto ret = av_hwdevice_ctx_init(tempDevice.get());
            if (ret < 0) {
                logInternal(LogLevel::Error, "Failed to init custom hardware device");
                return;
            }
            // Move the temp device to the internal one
            m_deviceContext = move(tempDevice);
        } else {
            // Create the hardware context for decoding
            AVBufferRef* deviceContext;
            const int err = av_hwdevice_ctx_create(&deviceContext, typeInternal, deviceName.c_str(), nullptr, 0);
            if (err < 0) {
                logInternal(LogLevel::Error, "Failed to create specified hardware device");
                return;
            }
            m_deviceContext = DeviceContextPtr(deviceContext);
        }
    }
}
} // namespace Ffr
