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
    using DecoderOptions = FfFrameReader::DecoderOptions;
    using DecodeType = FfFrameReader::DecodeType;

    /**
     * Constructor.
     * @param options (Optional) Options for controlling the context.
     */
    explicit DecoderContext(const DecoderOptions& options = DecoderOptions()) noexcept;

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