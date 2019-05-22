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

#include <any>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/log.h>
}

using namespace std;

namespace Ffr {
const DecoderContext::DeviceContextPtr& getDeviceContext(DecoderContext* context) noexcept
{
    return context->m_deviceContext;
}

enum AVHWDeviceType DecoderContext::decodeTypeToFFmpeg(const DecodeType type)
{
    if (type == DecodeType::CUDA) {
        return AV_HWDEVICE_TYPE_CUDA;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

DecoderContext::DeviceContextPtr::DeviceContextPtr(AVBufferRef* deviceContext) noexcept
    : m_deviceContext(deviceContext, [](AVBufferRef* p) { av_buffer_unref(&p); })
{}

AVBufferRef* DecoderContext::DeviceContextPtr::operator->() const noexcept
{
    return m_deviceContext.get();
}

AVBufferRef* DecoderContext::DeviceContextPtr::get() const noexcept
{
    return m_deviceContext.get();
}

DecoderContext::DecoderContext(const DecoderOptions& options) noexcept
{
    if (options.m_type != DecodeType::Software) {
        // Create device specific options
        string device;
        try {
            device = to_string(options.m_device);
        } catch (...) {
        }
        const auto type = decodeTypeToFFmpeg(options.m_type);

        // Check if we should create a custom hwdevice
        if (options.m_context.has_value()) {
            DeviceContextPtr tempDevice(av_hwdevice_ctx_alloc(type));
            if (tempDevice.get() == nullptr) {
                av_log(nullptr, AV_LOG_ERROR, "Failed to create custom hardware device\n");
                return;
            }
            auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(tempDevice->data);
            deviceContext->free = nullptr;
            if (type == AV_HWDEVICE_TYPE_CUDA) {
                if (options.m_context.type() != typeid(CUcontext)) {
                    av_log(nullptr, AV_LOG_ERROR, "Specified device context does not match the required type\n");
                    return;
                }
                auto* cudaDevice = reinterpret_cast<AVCUDADeviceContext*>(deviceContext->hwctx);
                try {
                    cudaDevice->cuda_ctx = any_cast<CUcontext>(options.m_context);
                } catch (...) {
                }
            }
            const auto ret = av_hwdevice_ctx_init(tempDevice.get());
            if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "Failed to init custom hardware device\n");
                return;
            }
            // Move the temp device to the internal one
            m_deviceContext = move(tempDevice);
        } else {
            // Create the hardware context for decoding
            AVBufferRef* deviceContext;
            const int err = av_hwdevice_ctx_create(&deviceContext, type, device.c_str(), nullptr, 0);
            if (err < 0) {
                av_log(nullptr, AV_LOG_ERROR, "Failed to create specified hardware device\n");
                return;
            }
            m_deviceContext = DeviceContextPtr(deviceContext);
        }
    }
}
} // namespace Ffr