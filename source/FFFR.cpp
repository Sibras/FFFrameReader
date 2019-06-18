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

#include <map>
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

extern "C" unsigned char FFFRFormatConvert[];

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

class FFR
{
private:
    struct NV12Planes
    {
        CUdeviceptr m_plane1;
        CUdeviceptr m_plane2;
    };

    struct RGBPlanes
    {
        CUdeviceptr m_plane1;
        CUdeviceptr m_plane2;
        CUdeviceptr m_plane3;
    };

    class KernelContext
    {
    public:
        CUmodule m_module = nullptr;
        CUfunction m_kernelNV12ToRGB8P = nullptr;
        int m_kernelNV12ToRGB8PMem = 0;
        CUfunction m_kernelNV12ToRGB32FP = nullptr;
        int m_kernelNV12ToRGB32FPMem = 0;
        CUstream m_stream = nullptr;

        explicit KernelContext(const CUstream stream)
        {
            auto err = cuModuleLoadData(&m_module, FFFRFormatConvert);
            if (err != CUDA_SUCCESS) {
                const char* errorString;
                cuGetErrorName(err, &errorString);
                log("Failed loading cuda module: "s += errorString, LogLevel::Error);
                return;
            }

            err = cuModuleGetFunction(&m_kernelNV12ToRGB8P, m_module, "convertNV12ToRGB8P");
            if (err != CUDA_SUCCESS) {
                const char* errorString;
                cuGetErrorName(err, &errorString);
                log("Failed to retrieve CUDA kernel: "s += errorString, LogLevel::Error);
                return;
            }

            err = cuModuleGetFunction(&m_kernelNV12ToRGB32FP, m_module, "convertNV12ToRGB32FP");
            if (err != CUDA_SUCCESS) {
                const char* errorString;
                cuGetErrorName(err, &errorString);
                log("Failed to retrieve CUDA kernel: "s += errorString, LogLevel::Error);
                return;
            }

            cuFuncGetAttribute(&m_kernelNV12ToRGB8PMem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, m_kernelNV12ToRGB8P);
            cuFuncGetAttribute(&m_kernelNV12ToRGB32FPMem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, m_kernelNV12ToRGB32FP);

            m_stream = stream;
        }

        ~KernelContext()
        {
            if (m_module != nullptr) {
                cuModuleUnload(m_module);
            }
        }

        [[nodiscard]] bool isValid() const
        {
            return m_kernelNV12ToRGB32FP != nullptr;
        }
    };

    static mutex s_mutex;
    static map<CUcontext, pair<NppStreamContext, shared_ptr<KernelContext>>> s_contextProperties;

    static bool setupContext(const CUcontext context, const CUstream stream)
    {
        // Check if context has already been configured
        if (s_contextProperties.find(context) != s_contextProperties.end()) {
            return true;
        }

        // Create Npp context
        NppStreamContext nppContext = {};
        nppContext.hStream = stream;
        cuCtxGetDevice(&nppContext.nCudaDeviceId);
        cuDeviceGetAttribute(
            &nppContext.nMultiProcessorCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, nppContext.nCudaDeviceId);
        cuDeviceGetAttribute(&nppContext.nMaxThreadsPerMultiProcessor,
            CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR, nppContext.nCudaDeviceId);
        cuDeviceGetAttribute(
            &nppContext.nMaxThreadsPerBlock, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, nppContext.nCudaDeviceId);
        int temp;
        cuDeviceGetAttribute(&temp, CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK, nppContext.nCudaDeviceId);
        nppContext.nSharedMemPerBlock = temp;
        cuDeviceGetAttribute(&nppContext.nCudaDevAttrComputeCapabilityMajor,
            CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, nppContext.nCudaDeviceId);
        cuDeviceGetAttribute(&nppContext.nCudaDevAttrComputeCapabilityMinor,
            CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, nppContext.nCudaDeviceId);

        // Create custom context properties
        auto kernelProperties = make_shared<KernelContext>(stream);
        if (!kernelProperties->isValid()) {
            return false;
        }

        // Add new properties to internal list
        s_contextProperties[context] = make_pair(nppContext, move(kernelProperties));
        return true;
    }

    static constexpr uint32_t divUp(const uint32_t total, const uint32_t grain)
    {
        return (total + grain - 1) / grain;
    }

    static CUresult convertNV12ToRGB8P(uint8_t* const source[2], uint32_t sourceStep, uint32_t width, uint32_t height,
        uint8_t* dest[3], uint32_t destStep, KernelContext* context)
    {
        const dim3 blockDim(8, 8, 1);
        const dim3 gridDim(divUp(width, blockDim.x), divUp(height, blockDim.y), 1);

        NV12Planes inMem = {reinterpret_cast<CUdeviceptr>(source[0]), reinterpret_cast<CUdeviceptr>(source[1])};
        RGBPlanes outMem = {reinterpret_cast<CUdeviceptr>(dest[0]), reinterpret_cast<CUdeviceptr>(dest[1]),
            reinterpret_cast<CUdeviceptr>(dest[2])};
        void* args[] = {&inMem, &sourceStep, &width, &height, &outMem, &destStep};
        return cuLaunchKernel(context->m_kernelNV12ToRGB8P, gridDim.x, gridDim.y, gridDim.z, blockDim.x, blockDim.y,
            blockDim.z, context->m_kernelNV12ToRGB8PMem, context->m_stream, args, nullptr);
    }

    static CUresult convertNV12ToRGB32FP(uint8_t* const source[2], uint32_t sourceStep, uint32_t width, uint32_t height,
        uint8_t* dest[3], uint32_t destStep, KernelContext* context)
    {
        const dim3 blockDim(8, 8, 1);
        const dim3 gridDim(divUp(width, blockDim.x), divUp(height, blockDim.y), 1);

        NV12Planes inMem = {reinterpret_cast<CUdeviceptr>(source[0]), reinterpret_cast<CUdeviceptr>(source[1])};
        RGBPlanes outMem = {reinterpret_cast<CUdeviceptr>(dest[0]), reinterpret_cast<CUdeviceptr>(dest[1]),
            reinterpret_cast<CUdeviceptr>(dest[2])};
        void* args[] = {&inMem, &sourceStep, &width, &height, &outMem, &destStep};
        return cuLaunchKernel(context->m_kernelNV12ToRGB32FP, gridDim.x, gridDim.y, gridDim.z, blockDim.x, blockDim.y,
            blockDim.z, context->m_kernelNV12ToRGB32FPMem, context->m_stream, args, nullptr);
    }

    static NppStatus cudaErrorToNppStatus(const CUresult err)
    {
        if (err == CUDA_SUCCESS) {
            return NPP_SUCCESS;
        }
        return NPP_CUDA_KERNEL_EXECUTION_ERROR;
    }

public:
    static bool convertFormat(
        const std::shared_ptr<Frame>& frame, uint8_t* outMem, const PixelFormat outFormat) noexcept
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
        const auto stream = cudaDevice->stream;
        NppStreamContext nppContext;
        shared_ptr<KernelContext> kernelProps;
        {
            lock_guard<mutex> lock(s_mutex);
            if (!setupContext(cudaDevice->cuda_ctx, stream)) {
                return false;
            }
            // Get required data
            nppContext = s_contextProperties[cudaDevice->cuda_ctx].first;
            kernelProps = s_contextProperties[cudaDevice->cuda_ctx].second;
        }

        NppiSize roi;
        roi.width = frame->getWidth();
        roi.height = frame->getHeight();

        uint8_t* outPlanes[4];
        int32_t outStep[4];
        av_image_fill_arrays(outPlanes, outStep, outMem, getPixelFormat(outFormat), roi.width, roi.height, 32);

        const auto data1 = frame->getFrameData(0);
        NppStatus ret = NPP_ERROR_RESERVED;
        switch (frame->getPixelFormat()) {
            case PixelFormat::YUV420P: {
                const auto data2 = frame->getFrameData(1);
                const auto data3 = frame->getFrameData(2);
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
                int32_t inStep[3] = {data1.second, data2.second, data3.second};
                switch (outFormat) {
                    case PixelFormat::RGB8P: {
                        ret = nppiYUV420ToRGB_8u_P3R_Ctx(inMem, inStep, outPlanes, outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = nppiYUV420ToRGB_8u_P3C3R_Ctx(inMem, inStep, outPlanes[0], outStep[0], roi, nppContext);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case PixelFormat::YUV422P: {
                const auto data2 = frame->getFrameData(1);
                const auto data3 = frame->getFrameData(2);
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
                int32_t inStep[3] = {data1.second, data2.second, data3.second};
                switch (outFormat) {
                    case PixelFormat::RGB8P: {
                        ret = nppiYUV422ToRGB_8u_P3R_Ctx(inMem, inStep, outPlanes, outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = nppiYUV422ToRGB_8u_P3C3R_Ctx(inMem, inStep, outPlanes[0], outStep[0], roi, nppContext);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case PixelFormat::YUV444P: {
                const auto data2 = frame->getFrameData(1);
                const auto data3 = frame->getFrameData(2);
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
                switch (outFormat) {
                    case PixelFormat::RGB8P: {
                        ret = nppiYUVToRGB_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = nppiYUVToRGB_8u_P3C3R_Ctx(inMem, data1.second, outPlanes[0], outStep[0], roi, nppContext);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case PixelFormat::NV12: {
                const auto data2 = frame->getFrameData(1);
                uint8_t* inMem[2] = {data1.first, data2.first};
                switch (outFormat) {
                    case PixelFormat::RGB8: {
                        ret =
                            nppiNV12ToRGB_8u_P2C3R_Ctx(inMem, data1.second, outPlanes[0], outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = nppiNV12ToYUV420_8u_P2P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext);
                        break;
                    }
                    case PixelFormat::RGB8P: {
                        ret = cudaErrorToNppStatus(convertNV12ToRGB8P(
                            inMem, data1.second, roi.width, roi.height, outPlanes, outStep[0], kernelProps.get()));
                        break;
                    }
                    case PixelFormat::RGB32FP: {
                        ret = cudaErrorToNppStatus(convertNV12ToRGB32FP(
                            inMem, data1.second, roi.width, roi.height, outPlanes, outStep[0], kernelProps.get()));
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
                        ret = nppiRGBToYUV_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::YUV422P: {
                        ret = nppiRGBToYUV422_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep, roi, nppContext);
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = nppiRGBToYUV420_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep, roi, nppContext);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case PixelFormat::RGB8P: {
                const auto data2 = frame->getFrameData(1);
                const auto data3 = frame->getFrameData(2);
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
                switch (outFormat) {
                    case PixelFormat::YUV444P: {
                        ret = nppiRGBToYUV_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep[0], roi, nppContext);
                        break;
                    }
                    case PixelFormat::YUV422P: {
                        ret = nppiRGBToYUV422_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext);
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = nppiRGBToYUV420_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext);
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
        const auto err = cuCtxSynchronize();
        CUcontext dummy;
        if (cuCtxPopCurrent(&dummy) != CUDA_SUCCESS) {
            log("Failed to restore CUDA context", LogLevel::Error);
        }
        if (ret != NPP_SUCCESS) {
            if (ret == NPP_ERROR_RESERVED) {
                log("Format conversion not currently supported", LogLevel::Error);
            } else if (ret == NPP_CUDA_KERNEL_EXECUTION_ERROR) {
                log("CUDA kernel for format conversion failed", LogLevel::Error);
            } else {
                log("Format conversion failed: "s += to_string(ret), LogLevel::Error);
            }
        }
        if (err != CUDA_SUCCESS) {
            const char* errorString;
            cuGetErrorName(err, &errorString);
            log("Format conversion failed: "s += errorString, LogLevel::Error);
            return false;
        }
        return true;
    }
};

bool convertFormat(const std::shared_ptr<Frame>& frame, uint8_t* outMem, const PixelFormat outFormat) noexcept
{
    return FFR::convertFormat(frame, outMem, outFormat);
}

mutex FFR::s_mutex;
map<CUcontext, pair<NppStreamContext, shared_ptr<FFR::KernelContext>>> FFR::s_contextProperties;
} // namespace Ffr