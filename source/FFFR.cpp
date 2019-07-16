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
#include "config.h"

#include <map>
#if BUILD_NPPI
#    include <nppi_color_conversion.h>
#endif
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
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

int32_t getImageLineStep(const PixelFormat format, const uint32_t width, const uint32_t plane) noexcept
{
    return FFALIGN(av_image_get_linesize(getPixelFormat(format), width, plane), 32);
}

int32_t getImagePlaneStep(
    const PixelFormat format, const uint32_t width, const uint32_t height, const uint32_t plane) noexcept
{
    if (static_cast<int32_t>(plane) >= getPixelFormatPlanes(format)) {
        return -1;
    }
    uint8_t* outPlanes[4];
    int32_t outStep[4];
    av_image_fill_arrays(outPlanes, outStep, nullptr, getPixelFormat(format), width, height, 32);
    const auto ret = plane == 0 ? reinterpret_cast<size_t>(outPlanes[0]) : outPlanes[plane] - outPlanes[plane - 1];
    return static_cast<int32_t>(ret);
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
        CUcontext m_context = nullptr;
        CUstream m_stream = nullptr;

        explicit KernelContext(const CUcontext context, const CUstream stream)
        {
            // Ensure that the primary context is retained until the module is unloaded
            CUcontext temp;
            cuDevicePrimaryCtxRetain(&temp, 0);
            if (temp != context) {
                cuDevicePrimaryCtxRelease(0);
            }

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

            m_context = context;
            m_stream = stream;
        }

        ~KernelContext()
        {
            if (m_context != nullptr) {
                // We cant guarantee the context hasnt been destroyed unless it is the primary context
                CUcontext temp;
                cuDevicePrimaryCtxRetain(&temp, 0);
                if (temp == m_context) {
                    if (cuCtxPushCurrent(m_context) != CUDA_SUCCESS) {
                        log("Failed to set CUDA context"s, LogLevel::Error);
                    }
                    if (m_module != nullptr) {
                        cuModuleUnload(m_module);
                    }
                    CUcontext dummy;
                    cuCtxPopCurrent(&dummy);
                    cuDevicePrimaryCtxRelease(0);
                    cuDevicePrimaryCtxRelease(0);
                }
            }
        }

        [[nodiscard]] bool isValid() const
        {
            return m_context != nullptr;
        }
    };

    static mutex s_mutex;
#if BUILD_NPPI
    static map<CUcontext, pair<NppStreamContext, shared_ptr<KernelContext>>> s_contextProperties;
#else
    static map<CUcontext, shared_ptr<KernelContext>> s_contextProperties;
#endif

    static bool setupContext(const CUcontext context, const CUstream stream)
    {
        // Check if context has already been configured
        if (s_contextProperties.find(context) != s_contextProperties.end()) {
            return true;
        }

#if BUILD_NPPI
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
#endif

        // Create custom context properties
        auto kernelProperties = make_shared<KernelContext>(context, stream);
        if (!kernelProperties->isValid()) {
            return false;
        }

        // Add new properties to internal list
#if BUILD_NPPI
        s_contextProperties[context] = make_pair(nppContext, move(kernelProperties));
#else
        s_contextProperties[context] = move(kernelProperties);
#endif
        return true;
    }

    static constexpr uint32_t divUp(const uint32_t total, const uint32_t grain)
    {
        return (total + grain - 1) / grain;
    }

    static CUresult convertNV12ToRGB8P(uint8_t* const source[2], uint32_t sourceStep, uint32_t width, uint32_t height,
        uint8_t* dest[3], uint32_t destStep, KernelContext* context)
    {
        const uint32_t blockX = 8;
        const uint32_t blockY = 8;

        NV12Planes inMem = {reinterpret_cast<CUdeviceptr>(source[0]), reinterpret_cast<CUdeviceptr>(source[1])};
        RGBPlanes outMem = {reinterpret_cast<CUdeviceptr>(dest[0]), reinterpret_cast<CUdeviceptr>(dest[1]),
            reinterpret_cast<CUdeviceptr>(dest[2])};
        void* args[] = {&inMem, &sourceStep, &width, &height, &outMem, &destStep};
        return cuLaunchKernel(context->m_kernelNV12ToRGB8P, divUp(width, blockX), divUp(height, blockY), 1, blockX,
            blockY, 1, context->m_kernelNV12ToRGB8PMem, context->m_stream, args, nullptr);
    }

    static CUresult convertNV12ToRGB32FP(uint8_t* const source[2], uint32_t sourceStep, uint32_t width, uint32_t height,
        uint8_t* dest[3], uint32_t destStep, KernelContext* context)
    {
        const uint32_t blockX = 8;
        const uint32_t blockY = 8;

        NV12Planes inMem = {reinterpret_cast<CUdeviceptr>(source[0]), reinterpret_cast<CUdeviceptr>(source[1])};
        RGBPlanes outMem = {reinterpret_cast<CUdeviceptr>(dest[0]), reinterpret_cast<CUdeviceptr>(dest[1]),
            reinterpret_cast<CUdeviceptr>(dest[2])};
        void* args[] = {&inMem, &sourceStep, &width, &height, &outMem, &destStep};
        return cuLaunchKernel(context->m_kernelNV12ToRGB32FP, divUp(width, blockX), divUp(height, blockY), 1, blockX,
            blockY, 1, context->m_kernelNV12ToRGB32FPMem, context->m_stream, args, nullptr);
    }

#if BUILD_NPPI
    static CUresult cudaNppStatusToError(const NppStatus err)
    {
        if (err == NPP_SUCCESS) {
            return CUDA_SUCCESS;
        }
        if (err == NPP_NO_MEMORY_ERROR) {
            return CUDA_ERROR_OUT_OF_MEMORY;
        }
        if (err == NPP_CUDA_KERNEL_EXECUTION_ERROR) {
            return CUDA_ERROR_LAUNCH_FAILED;
        }
        if (err == NPP_INVALID_DEVICE_POINTER_ERROR) {
            return CUDA_ERROR_ILLEGAL_ADDRESS;
        }
        return CUDA_ERROR_UNKNOWN;
    }
#endif

public:
    static bool convertFormat(
        const std::shared_ptr<Frame>& frame, uint8_t* outMem, const PixelFormat outFormat, bool asynch) noexcept
    {
        if (frame == nullptr || outMem == nullptr) {
            log("Invalid frame"s, LogLevel::Error);
            return false;
        }
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
#if BUILD_NPPI
        NppStreamContext nppContext;

        NppiSize roi;
        roi.width = frame->getWidth();
        roi.height = frame->getHeight();
#endif
        shared_ptr<KernelContext> kernelProps;
        {
            lock_guard<mutex> lock(s_mutex);
            if (!setupContext(cudaDevice->cuda_ctx, stream)) {
                return false;
            }
            // Get required data
#if BUILD_NPPI
            nppContext = s_contextProperties[cudaDevice->cuda_ctx].first;
            kernelProps = s_contextProperties[cudaDevice->cuda_ctx].second;
#else
            kernelProps = s_contextProperties[cudaDevice->cuda_ctx];
#endif
        }

        uint8_t* outPlanes[4];
        int32_t outStep[4];
        av_image_fill_arrays(
            outPlanes, outStep, outMem, getPixelFormat(outFormat), frame->getWidth(), frame->getHeight(), 32);

        const auto data1 = frame->getFrameData(0);
        CUresult ret = CUDA_ERROR_UNKNOWN;
        switch (frame->getPixelFormat()) {
#if BUILD_NPPI
            case PixelFormat::YUV420P: {
                const auto data2 = frame->getFrameData(1);
                const auto data3 = frame->getFrameData(2);
                const uint8_t* inMem[3] = {data1.first, data2.first, data3.first};
                int32_t inStep[3] = {data1.second, data2.second, data3.second};
                switch (outFormat) {
                    case PixelFormat::RGB8P: {
                        ret = cudaNppStatusToError(
                            nppiYUV420ToRGB_8u_P3R_Ctx(inMem, inStep, outPlanes, outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = cudaNppStatusToError(
                            nppiYUV420ToRGB_8u_P3C3R_Ctx(inMem, inStep, outPlanes[0], outStep[0], roi, nppContext));
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
                        ret = cudaNppStatusToError(
                            nppiYUV422ToRGB_8u_P3R_Ctx(inMem, inStep, outPlanes, outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = cudaNppStatusToError(
                            nppiYUV422ToRGB_8u_P3C3R_Ctx(inMem, inStep, outPlanes[0], outStep[0], roi, nppContext));
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
                        ret = cudaNppStatusToError(
                            nppiYUVToRGB_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::RGB8: {
                        ret = cudaNppStatusToError(
                            nppiYUVToRGB_8u_P3C3R_Ctx(inMem, data1.second, outPlanes[0], outStep[0], roi, nppContext));
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
#endif
            case PixelFormat::NV12: {
                const auto data2 = frame->getFrameData(1);
                uint8_t* inMem[2] = {data1.first, data2.first};
                switch (outFormat) {
#if BUILD_NPPI
                    case PixelFormat::RGB8: {
                        ret = cudaNppStatusToError(
                            nppiNV12ToRGB_8u_P2C3R_Ctx(inMem, data1.second, outPlanes[0], outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = cudaNppStatusToError(
                            nppiNV12ToYUV420_8u_P2P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext));
                        break;
                    }
#endif
                    case PixelFormat::RGB8P: {
                        ret = convertNV12ToRGB8P(inMem, data1.second, frame->getWidth(), frame->getHeight(), outPlanes,
                            outStep[0], kernelProps.get());
                        break;
                    }
                    case PixelFormat::RGB32FP: {
                        ret = convertNV12ToRGB32FP(inMem, data1.second, frame->getWidth(), frame->getHeight(),
                            outPlanes, outStep[0], kernelProps.get());
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
#if BUILD_NPPI
            case PixelFormat::RGB8: {
                switch (outFormat) {
                    case PixelFormat::YUV444P: {
                        ret = cudaNppStatusToError(nppiRGBToYUV_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::YUV422P: {
                        ret = cudaNppStatusToError(nppiRGBToYUV422_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep, roi, nppContext));
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = cudaNppStatusToError(nppiRGBToYUV420_8u_C3P3R_Ctx(
                            data1.first, data1.second, outPlanes, outStep, roi, nppContext));
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
                        ret = cudaNppStatusToError(
                            nppiRGBToYUV_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep[0], roi, nppContext));
                        break;
                    }
                    case PixelFormat::YUV422P: {
                        ret = cudaNppStatusToError(
                            nppiRGBToYUV422_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext));
                        break;
                    }
                    case PixelFormat::YUV420P: {
                        ret = cudaNppStatusToError(
                            nppiRGBToYUV420_8u_P3R_Ctx(inMem, data1.second, outPlanes, outStep, roi, nppContext));
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
#endif
            default:
                break;
        }
        if (ret != CUDA_SUCCESS) {
            if (ret == CUDA_ERROR_UNKNOWN) {
                log("Format conversion not currently supported", LogLevel::Error);
            } else if (ret == CUDA_ERROR_LAUNCH_FAILED) {
                log("CUDA kernel for format conversion failed", LogLevel::Error);
            } else {
                const char* errorString;
                cuGetErrorName(ret, &errorString);
                log("Format conversion failed: "s += errorString, LogLevel::Error);
            }
        }
        if (!asynch) {
            ret = cuCtxSynchronize();
            if (ret != CUDA_SUCCESS) {
                const char* errorString;
                cuGetErrorName(ret, &errorString);
                log("Format conversion failed: "s += errorString, LogLevel::Error);
            }
        }
        CUcontext dummy;
        if (cuCtxPopCurrent(&dummy) != CUDA_SUCCESS) {
            log("Failed to restore CUDA context", LogLevel::Error);
        }
        return (ret == CUDA_SUCCESS);
    }

    static bool synchroniseConvert(const std::shared_ptr<Stream>& stream) noexcept
    {
        if (stream == nullptr || stream->m_codecContext->pix_fmt != AV_PIX_FMT_CUDA || stream->m_outputHost) {
            log("Invalid stream"s, LogLevel::Error);
            return false;
        }
        CUcontext context = nullptr;
        if (stream->m_codecContext->hw_frames_ctx == nullptr) {
            // This means its the cuvid decoder which just uses the default context
            CUdevice dev;
            cuDeviceGet(&dev, 0);
            cuDevicePrimaryCtxRetain(&context, dev);
        } else {
            auto* framesContext = reinterpret_cast<AVHWFramesContext*>(stream->m_codecContext->hw_frames_ctx->data);
            auto* cudaDevice = reinterpret_cast<AVCUDADeviceContext*>(framesContext->device_ctx->hwctx);
            context = cudaDevice->cuda_ctx;
        }
        if (cuCtxPushCurrent(context) != CUDA_SUCCESS) {
            log("Failed to set CUDA context"s, LogLevel::Error);
            return false;
        }
        const auto err = cuCtxSynchronize();
        CUcontext dummy;
        if (cuCtxPopCurrent(&dummy) != CUDA_SUCCESS) {
            log("Failed to restore CUDA context", LogLevel::Error);
        }
        if (err != CUDA_SUCCESS) {
            const char* errorString;
            cuGetErrorName(err, &errorString);
            log("Hardware synchronisation failed: "s += errorString, LogLevel::Error);
            return false;
        }
        return true;
    }
};

bool convertFormat(const std::shared_ptr<Frame>& frame, uint8_t* outMem, const PixelFormat outFormat) noexcept
{
    return FFR::convertFormat(frame, outMem, outFormat, false);
}

bool convertFormatAsync(const std::shared_ptr<Frame>& frame, uint8_t* outMem, const PixelFormat outFormat) noexcept
{
    return FFR::convertFormat(frame, outMem, outFormat, true);
}

bool synchroniseConvert(const std::shared_ptr<Stream>& stream)
{
    return FFR::synchroniseConvert(stream);
}

mutex FFR::s_mutex;
#if BUILD_NPPI
map<CUcontext, pair<NppStreamContext, shared_ptr<FFR::KernelContext>>> FFR::s_contextProperties;
#else
map<CUcontext, shared_ptr<FFR::KernelContext>> FFR::s_contextProperties;
#endif
} // namespace Ffr