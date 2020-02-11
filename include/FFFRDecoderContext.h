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
    friend class Stream;

public:
    using DecodeType = Ffr::DecodeType;

    /**
     * Constructor.
     * @param type    The type of decoding to use.
     * @param context Pointer to an existing context to be used for hardware decoding. This must match the hardware
     *  type specified in @m_type.
     * @param device  The device index for the desired hardware device.
     */
    FFFRAMEREADER_NO_EXPORT explicit DecoderContext(DecodeType type, const std::any& context, uint32_t device) noexcept;

    FFFRAMEREADER_NO_EXPORT ~DecoderContext() noexcept = default;

    FFFRAMEREADER_NO_EXPORT DecoderContext(const DecoderContext& other) = default;

    FFFRAMEREADER_NO_EXPORT DecoderContext(DecoderContext&& other) = default;

    FFFRAMEREADER_NO_EXPORT DecoderContext& operator=(const DecoderContext& other) = default;

    FFFRAMEREADER_NO_EXPORT DecoderContext& operator=(DecoderContext&& other) = default;

private:
    class DeviceContextPtr
    {
        friend class DecoderContext;
        friend class Stream;

        FFFRAMEREADER_NO_EXPORT DeviceContextPtr() = default;

        FFFRAMEREADER_NO_EXPORT explicit DeviceContextPtr(AVBufferRef* deviceContext) noexcept;

        FFFRAMEREADER_NO_EXPORT AVBufferRef* get() const noexcept;

        FFFRAMEREADER_NO_EXPORT AVBufferRef* operator->() const noexcept;

        std::shared_ptr<AVBufferRef> m_deviceContext = nullptr;
    };

    DeviceContextPtr m_deviceContext = DeviceContextPtr(nullptr);

    FFFRAMEREADER_NO_EXPORT static enum AVHWDeviceType DecodeTypeToFFmpeg(DecodeType type) noexcept;

    using FormatFunction = enum AVPixelFormat (*)(struct AVCodecContext*, const enum AVPixelFormat*);

    FFFRAMEREADER_NO_EXPORT FormatFunction getFormatFunction() const noexcept;

    FFFRAMEREADER_NO_EXPORT DecodeType getType() const noexcept;
};
} // namespace Ffr
