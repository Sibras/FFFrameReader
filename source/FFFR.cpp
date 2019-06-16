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
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}

using namespace std;

namespace Ffr {
void setLogLevel(const LogLevel level) noexcept
{
    av_log_set_level(static_cast<int>(level));
}

void log(const std::string& text, const LogLevel level) noexcept
{
    av_log(nullptr, static_cast<int>(level), "%s\n", text.c_str());
}

int32_t getPixelFormatPlanes(const PixelFormat format) noexcept
{
    return av_pix_fmt_count_planes(getPixelFormat(format));
}

int32_t getImageSize(const PixelFormat format, const uint32_t width, const uint32_t height) noexcept
{
    return av_image_get_buffer_size(getPixelFormat(format), width, height, 32);
}

extern cudaError_t convertNV12ToRGB8P(const uint8_t* const source[2], uint32_t sourceStep, uint32_t width,
    uint32_t height, uint8_t* dest[3], uint32_t destStep);

extern cudaError_t convertNV12ToRGB32FP(const uint8_t* const source[2], uint32_t sourceStep, uint32_t width,
    uint32_t height, uint8_t* dest[3], uint32_t destStep);

static NppStatus cudaErrorToNppStatus(const cudaError_t err)
{
    if (err == cudaSuccess) {
        return NPP_SUCCESS;
    }
    return NPP_CUDA_KERNEL_EXECUTION_ERROR;
}

class FFR
{
public:
    static bool convertFormat(
        const std::shared_ptr<Frame>& frame, uint8_t* outMem[3], const PixelFormat outFormat) noexcept
    {
        // This only supports cuda frames
        if (frame->getDataType() != DecodeType::Cuda) {
            log("Only CUDA frames are currently supported by convertFormat"s, LogLevel::Error);
            return false;
        }
        auto* framesContext = reinterpret_cast<AVHWFramesContext*>(frame->m_frame->hw_frames_ctx->data);
        auto* cudaDevice = reinterpret_cast<AVCUDADeviceContext*>(framesContext->device_ctx->hwctx);
        if (cuCtxPushCurrent(cudaDevice->cuda_ctx) != CUDA_SUCCESS) {
            log("Failed to set CUDA context"s, LogLevel::Error);
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
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
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
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
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
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
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
                const uint8_t* inMem[2] = {data1.first, data2.first};
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
                    case PixelFormat::GBR8P: {
                        av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::GBR8P), roi.width);
                        ret = cudaErrorToNppStatus(
                            convertNV12ToRGB8P(inMem, data1.second, roi.width, roi.height, outMem, outStep[0]));
                        break;
                    }
                    case PixelFormat::GBR32FP: {
                        av_image_fill_linesizes(outStep, getPixelFormat(PixelFormat::GBR32FP), roi.width);
                        ret = cudaErrorToNppStatus(
                            convertNV12ToRGB32FP(inMem, data1.second, roi.width, roi.height, outMem, outStep[0]));
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
                const uint8_t* inMem[3] = {data3.first, data1.first, data2.first};
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
        CUcontext dummy;
        if (cuCtxPopCurrent(&dummy) != CUDA_SUCCESS) {
            log("Failed to restore CUDA context", LogLevel::Error);
            return false;
        }
        if (ret != NPP_SUCCESS) {
            if (ret == NPP_ERROR_RESERVED) {
                log("Format conversion not currently supported", LogLevel::Error);
            } else if (ret == NPP_CUDA_KERNEL_EXECUTION_ERROR) {
                log("CUDA kernel for format conversion failed: "s += cudaGetErrorName(cudaGetLastError()),
                    LogLevel::Error);
            } else {
                log("Format conversion failed: "s += to_string(ret), LogLevel::Error);
            }
            return false;
        }
        return true;
    }
};

bool convertFormat(const std::shared_ptr<Frame>& frame, uint8_t* outMem[3], const PixelFormat outFormat) noexcept
{
    return FFR::convertFormat(frame, outMem, outFormat);
}
} // namespace Ffr