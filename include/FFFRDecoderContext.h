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
#pragma once
#include "FFFrameReader.h"

#include <functional>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
}

namespace Ffr {
class DecoderContext
{
    friend class FfFrameReader;

public:
    using DecodeType = FfFrameReader::DecodeType;

    class ContextOptions
    {
    public:
        ContextOptions() = default;

        ContextOptions(DecodeType type, std::any context, uint32_t device);

        ~ContextOptions() = default;

        ContextOptions(const ContextOptions& other) = default;

        ContextOptions(ContextOptions&& other) = default;

        ContextOptions& operator=(const ContextOptions& other) = default;

        ContextOptions& operator=(ContextOptions&& other) = default;

        DecodeType m_type = DecodeType::Software; /**< The type of decoding to use. */
        std::any m_context;                       /**< Pointer to an existing context to be used for hardware
                                                   decoding. This must match the hardware type specified in @m_type. */
        uint32_t m_device = 0;                    /**< The device index for the desired hardware device. */
    };

    /**
     * Constructor.
     * @param options Options for controlling the context.
     */
    explicit DecoderContext(const ContextOptions& options) noexcept;

    ~DecoderContext() noexcept = default;

    DecoderContext(const DecoderContext& other) = default;

    DecoderContext(DecoderContext&& other) = default;

    DecoderContext& operator=(const DecoderContext& other) = default;

    DecoderContext& operator=(DecoderContext&& other) = default;

private:
    class DeviceContextPtr
    {
        friend class DecoderContext;
        friend class FfFrameReader;

    public:
        explicit DeviceContextPtr(AVBufferRef* deviceContext) noexcept;

        [[nodiscard]] AVBufferRef* get() const noexcept;

        AVBufferRef* operator->() const noexcept;

    private:
        std::shared_ptr<AVBufferRef> m_deviceContext = nullptr;
    };

    DeviceContextPtr m_deviceContext = DeviceContextPtr(nullptr);

    friend const DeviceContextPtr& getDeviceContext(DecoderContext* context) noexcept;

    static enum AVHWDeviceType decodeTypeToFFmpeg(DecodeType type);
};
} // namespace Ffr