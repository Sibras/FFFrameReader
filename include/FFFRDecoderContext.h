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

    /**
     * Constructor.
     * @note bufferLength should be optimised based on reading/seeking pattern so as to minimise frame storage
     * requirements but also maximise decode throughput.
     * @param type         (Optional) The type of decoding to use.
     * @param bufferLength (Optional) Number of frames in the the decode buffer.
     */
    explicit DecoderContext(DecodeType type = DecodeType::Software, uint32_t bufferLength = 10) noexcept;

    ~DecoderContext() noexcept;

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
    DecodeType m_deviceType = DecodeType::Software;
    uint32_t m_bufferLength = 10;
    AVBufferRef* m_deviceContext = nullptr;
};
} // namespace FfFrameReader