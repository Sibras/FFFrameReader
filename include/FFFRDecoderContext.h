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
#include "FFFRStream.h"

#include <any>

struct AVBufferRef;

namespace FfFrameReader {
class DecoderContext
{
public:
    enum class DecodeType
    {
        Software,
        Nvdec,
#if 0
#    if defined(WIN32)
        Dxva2,
        D3d11va,
#    else
        Vaapi,
        Vdpau,
#    endif
        Qsv,
#endif
    };

    class DecoderOptions
    {
    public:
        DecodeType m_type = DecodeType::Software; /**< The type of decoding to use. */
        uint32_t m_bufferLength = 10;             /**< Number of frames in the the decode buffer.
                                                  This should be optimised based on reading/seeking pattern so as to minimise frame
                                                  storage requirements but also maximise decode throughput. */
        std::any m_context;    /**< Pointer to an existing context to be used for hardware decoding. This must
                                match the hardware type specified in @m_type. */
        uint32_t m_device = 0; /**< The device index for the desired hardware device. */

        DecoderOptions() = default;

        explicit DecoderOptions(DecodeType type) noexcept;

        ~DecoderOptions() = default;

        DecoderOptions(const DecoderOptions& other) = default;

        DecoderOptions(DecoderOptions&& other) noexcept = default;

        DecoderOptions& operator=(const DecoderOptions& other) = default;

        DecoderOptions& operator=(DecoderOptions&& other) noexcept = default;

        bool operator==(const DecoderOptions& other) const noexcept;

        bool operator!=(const DecoderOptions& other) const noexcept;

        bool operator<(const DecoderOptions& other) const noexcept;
    };

    /**
     * Constructor.
     * @param options (Optional) Options for controlling decoding.
     */
    explicit DecoderContext(const DecoderOptions& options = {}) noexcept;

    ~DecoderContext() noexcept = default;

    DecoderContext(const DecoderContext& other) noexcept = default;

    DecoderContext(DecoderContext&& other) noexcept = default;

    DecoderContext& operator=(const DecoderContext& other) noexcept = default;

    DecoderContext& operator=(DecoderContext&& other) noexcept = default;

    /**
     * Gets a stream from a file.
     * @param filename Filename of the file to open.
     * @returns The stream if succeeded, false otherwise.
     */
    [[nodiscard]] std::variant<bool, std::shared_ptr<Stream>> getStream(const std::string& filename) const noexcept;

private:
    class DeviceContextPtr
    {
        friend class DecoderContext;

    private:
        explicit DeviceContextPtr(AVBufferRef* deviceContext) noexcept;

        [[nodiscard]] AVBufferRef* get() const noexcept;

        AVBufferRef* operator->() const noexcept;

        std::shared_ptr<AVBufferRef> m_deviceContext = nullptr;
    };

    DecodeType m_deviceType = DecodeType::Software;
    uint32_t m_bufferLength = 10;
    DeviceContextPtr m_deviceContext = DeviceContextPtr(nullptr);
};
} // namespace FfFrameReader