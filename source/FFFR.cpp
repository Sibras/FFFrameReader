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
#include "FFFRUtility.h"
#include "FFFrameReader.h"

#include <nppi_color_conversion.h>
#include <string>

extern "C" {
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}

using namespace std;

namespace Ffr {
DecoderOptions::DecoderOptions(const DecodeType type) noexcept
    : m_type(type)
{}

bool DecoderOptions::operator==(const DecoderOptions& other) const noexcept
{
    const bool ret = this->m_type == other.m_type && this->m_bufferLength == other.m_bufferLength &&
        this->m_device == other.m_device;
    if (!ret) {
        return ret;
    }
    if (this->m_type == DecodeType::Software) {
        return true;
    }
    try {
        if (this->m_type == DecodeType::Cuda) {
            return any_cast<CUcontext>(this->m_context) == any_cast<CUcontext>(other.m_context);
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool DecoderOptions::operator!=(const DecoderOptions& other) const noexcept
{
    return !(*this == other);
}

bool DecoderOptions::operator<(const DecoderOptions& other) const noexcept
{
    if (this->m_type < other.m_type) {
        return true;
    }
    if (other.m_type < this->m_type) {
        return false;
    }
    if (this->m_bufferLength < other.m_bufferLength) {
        return true;
    }
    if (other.m_bufferLength < this->m_bufferLength) {
        return false;
    }
    try {
        if (this->m_type == DecodeType::Cuda) {
            if (any_cast<CUcontext>(this->m_context) < any_cast<CUcontext>(other.m_context)) {
                return true;
            }
            if (any_cast<CUcontext>(other.m_context) < any_cast<CUcontext>(this->m_context)) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return this->m_device < other.m_device;
}

void setLogLevel(const LogLevel level)
{
    av_log_set_level(static_cast<int>(level));
}

void log(const std::string& text, const LogLevel level)
{
    av_log(nullptr, static_cast<int>(level), "%s\n", text.c_str());
}

int32_t getPixelFormatPlanes(const PixelFormat format)
{
    return av_pix_fmt_count_planes(getPixelFormat(format));
}

int32_t getImageSize(const PixelFormat format, const uint32_t width, const uint32_t height)
{
    return av_image_get_buffer_size(getPixelFormat(format), width, height, 32);
}

bool convertFormat(const std::shared_ptr<Frame>& frame, uint8_t* outMem[3], const PixelFormat outFormat) noexcept
{
    // This only supports cuda frames
    if (frame->getDataType() != DecodeType::Cuda) {
        log("Only CUDA frames are currently supported by convertFormat", LogLevel::Error);
        return false;
    }
    NppiSize roi;
    roi.width = frame->getWidth();
    roi.height = frame->getHeight();
    const auto data1 = frame->getFrameData(1);
    int32_t outStep[4];
    NppStatus ret = NPP_ERROR_RESERVED;
    switch (frame->getPixelFormat()) {
        case PixelFormat::YUV420P: {
            const auto data2 = frame->getFrameData(2);
            const auto data3 = frame->getFrameData(3);
            uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
            int32_t inStep[3] = {data1.second, data2.second, data3.second};
            switch (outFormat) {
                case PixelFormat::GBR8P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::GBR8P), roi.width);
                    uint8_t* outMem2[3] = {outMem[1], outMem[2], outMem[0]};
                    ret = nppiYUV420ToRGB_8u_P3R(inMem, inStep, outMem2, outStep[0], roi);
                    break;
                }
                case PixelFormat::RGB8: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::RGB8), roi.width);
                    ret = nppiYUV420ToRGB_8u_P3C3R(inMem, inStep, outMem[0], outStep[0], roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case PixelFormat::YUV422P: {
            const auto data2 = frame->getFrameData(2);
            const auto data3 = frame->getFrameData(3);
            uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
            int32_t inStep[3] = {data1.second, data2.second, data3.second};
            switch (outFormat) {
                case PixelFormat::GBR8P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::GBR8P), roi.width);
                    uint8_t* outMem2[3] = {outMem[1], outMem[2], outMem[0]};
                    ret = nppiYUV422ToRGB_8u_P3R(inMem, inStep, outMem2, outStep[0], roi);
                    break;
                }
                case PixelFormat::RGB8: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::RGB8), roi.width);
                    ret = nppiYUV422ToRGB_8u_P3C3R(inMem, inStep, outMem[0], outStep[0], roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case PixelFormat::YUV444P: {
            const auto data2 = frame->getFrameData(2);
            const auto data3 = frame->getFrameData(3);
            uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
            switch (outFormat) {
                case PixelFormat::GBR8P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::GBR8P), roi.width);
                    uint8_t* outMem2[3] = {outMem[1], outMem[2], outMem[0]};
                    ret = nppiYUVToRGB_8u_P3R(inMem, data1.second, outMem2, outStep[0], roi);
                    break;
                }
                case PixelFormat::RGB8: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::RGB8), roi.width);
                    ret = nppiYUVToRGB_8u_P3C3R(inMem, data1.second, outMem[0], outStep[0], roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case PixelFormat::NV12: {
            const auto data2 = frame->getFrameData(2);
            uint8_t* inMem[2] = {data1.first, data2.first};
            switch (outFormat) {
                case PixelFormat::RGB8: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::RGB8), roi.width);
                    ret = nppiNV12ToRGB_8u_P2C3R(inMem, data1.second, outMem[0], outStep[0], roi);
                    break;
                }
                case PixelFormat::YUV420P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV420P), roi.width);
                    ret = nppiNV12ToYUV420_8u_P2P3R(inMem, data1.second, outMem, outStep, roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case PixelFormat::RGB8: {
            switch (outFormat) {
                case PixelFormat::YUV444P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV444P), roi.width);
                    ret = nppiRGBToYUV_8u_C3P3R(data1.first, data1.second, outMem, outStep[0], roi);
                    break;
                }
                case PixelFormat::YUV422P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV422P), roi.width);
                    ret = nppiRGBToYUV422_8u_C3P3R(data1.first, data1.second, outMem, outStep, roi);
                    break;
                }
                case PixelFormat::YUV420P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV420P), roi.width);
                    ret = nppiRGBToYUV420_8u_C3P3R(data1.first, data1.second, outMem, outStep, roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case PixelFormat::GBR8P: {
            const auto data2 = frame->getFrameData(2);
            const auto data3 = frame->getFrameData(3);
            uint8_t* inMem[3] = {data3.first, data1.first, data2.first};
            switch (outFormat) {
                case PixelFormat::YUV444P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV444P), roi.width);
                    ret = nppiRGBToYUV_8u_P3R(inMem, data1.second, outMem, outStep[0], roi);
                    break;
                }
                case PixelFormat::YUV422P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV422P), roi.width);
                    ret = nppiRGBToYUV422_8u_P3R(inMem, data1.second, outMem, outStep, roi);
                    break;
                }
                case PixelFormat::YUV420P: {
                    av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::YUV420P), roi.width);
                    ret = nppiRGBToYUV420_8u_P3R(inMem, data1.second, outMem, outStep, roi);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
    if (ret != NPP_SUCCESS) {
        if (ret == NPP_ERROR_RESERVED) {
            log("Format conversion not currently supported", LogLevel::Error);
        } else {
            log("Format conversion failed: "s += to_string(ret), LogLevel::Error);
        }
        return false;
    }
    return true;
}
} // namespace Ffr