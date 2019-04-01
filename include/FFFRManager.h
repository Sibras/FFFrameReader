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
#include "FFFRLog.h"
#include "FFFRStream.h"

extern "C" {
#include <libavutil/hwcontext.h>
}
#include <map>

namespace FfFrameReader {
class Manager
{
public:
    enum class DecodeType
    {
        Nvdec = AV_HWDEVICE_TYPE_CUDA,
#if 0
#    if defined(WIN32)
        Dxva2 = AV_HWDEVICE_TYPE_DXVA2,
        D3d11va = AV_HWDEVICE_TYPE_D3D11VA,
#    else
        Vaapi = AV_HWDEVICE_TYPE_VAAPI,
        Vdpau = AV_HWDEVICE_TYPE_VDPAU,
#    endif
        Qsv = AV_HWDEVICE_TYPE_QSV,
#endif
        Software = AV_HWDEVICE_TYPE_NONE,
    };

    /**
     * Constructor.
     * @note bufferLength should be optimised based on reading/seeking pattern so as to minimise frame storage
     * requirements but also maximise decode throughput.
     * @param type         (Optional) The type of decoding to use.
     * @param bufferLength (Optional) Number of frames in the the decode buffer.
     */
    explicit Manager(DecodeType type = DecodeType::Software, uint32_t bufferLength = 10) noexcept;

    ~Manager() noexcept;

    Manager(const Manager& other) noexcept = default;

    Manager(Manager&& other) noexcept = default;

    Manager& operator=(const Manager& other) noexcept = default;

    Manager& operator=(Manager&& other) noexcept = default;

    /**
     * Gets a stream from a file.
     * @note Once a stream has been finished with it should be released using releaseStream().
     * @param filename Filename of the file to open.
     * @returns The stream if succeeded, false otherwise.
     */
    std::variant<bool, std::shared_ptr<Stream>> getStream(const std::string& filename) noexcept;

    /**
     * Releases the stream described by filename
     * @param filename Filename of the file.
     */
    void releaseStream(const std::string& filename) noexcept;

private:
    enum AVHWDeviceType m_deviceType = AV_HWDEVICE_TYPE_NONE;
    uint32_t m_bufferLength = 10;
    AVBufferRef* m_deviceContext = nullptr;
    std::map<std::string, std::shared_ptr<Stream>> m_streams;
};
} // namespace FfFrameReader