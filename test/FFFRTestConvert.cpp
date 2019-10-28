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
#include "FFFRConfig.h"
#if FFFR_BUILD_CUDA
#    include "FFFRTestData.h"
#    include "FFFRUtility.h"
#    include "FFFrameReader.h"

#    include <cuda.h>
#    include <fstream>
#    include <gtest/gtest.h>

extern "C" {
#    include <libavutil/imgutils.h>
}

using namespace Ffr;

extern void saveImage(PixelFormat format, uint32_t width, uint32_t height, const std::string& filename,
    uint8_t* buffer[4], int32_t step[4]) noexcept;

struct TestParamsConvert
{
    uint32_t m_testDataIndex;
    PixelFormat m_format;
    std::string m_imageFile;
};

static std::vector<TestParamsConvert> g_testDataConvert = {
#    if FFFR_BUILD_NPPI
    {1, PixelFormat::RGB8, "test-convert-1"},
    {1, PixelFormat::YUV420P, "test-convert-2"},
#    endif
    {1, PixelFormat::RGB8P, "test-convert-3"},
    {1, PixelFormat::RGB32FP, "test-convert-4"},
#    if FFFR_BUILD_NPPI
    {3, PixelFormat::RGB8, "test-convert-5"},
    {3, PixelFormat::YUV420P, "test-convert-6"},
#    endif
    {3, PixelFormat::RGB8P, "test-convert-7"},
    {3, PixelFormat::RGB32FP, "test-convert-8"},
};

class TestConvert
{
public:
    TestConvert() = default;

    void SetUp(const TestParamsConvert& params)
    {
        // Create a cuda context to be shared by decoder and this test
        ASSERT_EQ(cuInit(0), CUDA_SUCCESS);
        CUcontext context = nullptr;
        ASSERT_EQ(cuCtxGetCurrent(&context), CUDA_SUCCESS);
        if (m_context == nullptr) {
            CUdevice dev;
            ASSERT_EQ(cuDeviceGet(&dev, 0), CUDA_SUCCESS);
            ASSERT_EQ(cuDevicePrimaryCtxRetain(&context, dev), CUDA_SUCCESS);
            m_context = std::shared_ptr<std::remove_pointer<CUcontext>::type>(
                context, [dev](CUcontext) { cuDevicePrimaryCtxRelease(dev); });
        } else {
            m_context = std::shared_ptr<std::remove_pointer<CUcontext>::type>(context, [](CUcontext) {});
        }

        DecoderOptions options;
        options.m_type = DecodeType::Cuda;
        options.m_outputHost = false;
        options.m_bufferLength = 1;
        options.m_context = m_context.get();
        m_stream = Stream::getStream(g_testData[params.m_testDataIndex].m_fileName, options);
        ASSERT_NE(m_stream, nullptr);

        // Get frame dimensions
        const int width = m_stream->peekNextFrame()->getWidth();
        const int height = m_stream->peekNextFrame()->getHeight();

        // Allocate new memory to store frame data
        ASSERT_EQ(cuCtxPushCurrent(m_context.get()), CUDA_SUCCESS);
        const auto outFrameSize = getImageSize(params.m_format, width, height) +
            getImagePlaneStep(params.m_format, width, height, 0); // extra added to test for stomping
        ASSERT_EQ(cuMemAlloc(&m_cudaBuffer, outFrameSize), CUDA_SUCCESS);
        ASSERT_EQ(cuMemsetD8(m_cudaBuffer, 254, outFrameSize), CUDA_SUCCESS);
        CUcontext dummy;
        ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);
    }

    void TearDown()
    {
        m_stream.reset();
        if (reinterpret_cast<void*>(m_cudaBuffer) != nullptr) {
            ASSERT_EQ(cuCtxPushCurrent(m_context.get()), CUDA_SUCCESS);
            ASSERT_EQ(cuMemFree(m_cudaBuffer), CUDA_SUCCESS);
            CUcontext dummy;
            ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);
        }
        m_context = nullptr;
    }

    void saveImage(
        const PixelFormat format, const uint32_t width, const uint32_t height, const std::string& filename) const
    {
        if (format != PixelFormat::RGB32FP && format != PixelFormat::RGB8P && format != PixelFormat::RGB8) {
            return;
        }
        // Copy data to host
        std::vector<uint8_t> hostBuffer;
        const auto padding = getImagePlaneStep(format, width, height, 0);
        const auto imageSize = getImageSize(format, width, height) + padding;
        hostBuffer.reserve(imageSize);
        ASSERT_EQ(cuCtxPushCurrent(m_context.get()), CUDA_SUCCESS);
        ASSERT_EQ(cuMemcpyDtoH(hostBuffer.data(), m_cudaBuffer, imageSize), CUDA_SUCCESS);
        ASSERT_EQ(cuCtxSynchronize(), CUDA_SUCCESS);
        CUcontext dummy;
        ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);

        // Get each data frame
        uint8_t* outPlanes[4];
        int32_t outStep[4];
        av_image_fill_arrays(outPlanes, outStep, hostBuffer.data(), getPixelFormat(format), width, height, 32);

        ::saveImage(format, width, height, filename, outPlanes, outStep);

        // Check for memory stomping
        for (int32_t i = 0; i < padding; i++) {
            ASSERT_EQ(hostBuffer.data()[imageSize - padding + i], 254);
        }
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    std::shared_ptr<std::remove_pointer<CUcontext>::type> m_context = nullptr;
    CUdeviceptr m_cudaBuffer = reinterpret_cast<CUdeviceptr>(nullptr);
};

class ConvertTest1 : public ::testing::TestWithParam<TestParamsConvert>
{
protected:
    ConvertTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Warning);
        m_decoder.SetUp(GetParam());
    }

    void TearDown() override
    {
        m_decoder.TearDown();
    }

    TestConvert m_decoder;
};

TEST_P(ConvertTest1, convert)
{
    for (uint32_t j = 0; j < 3; j++) {
        const auto frame1 = m_decoder.m_stream->getNextFrame();
        ASSERT_NE(frame1, nullptr);

        // Check if known pixel format
        ASSERT_NE(frame1->getPixelFormat(), Ffr::PixelFormat::Auto);

        // Copy/Convert image data into output
        ASSERT_TRUE(convertFormat(frame1, reinterpret_cast<uint8_t*>(m_decoder.m_cudaBuffer), GetParam().m_format));

        // Save to image for visual inspection
        m_decoder.saveImage(GetParam().m_format, frame1->getWidth(), frame1->getHeight(),
            GetParam().m_imageFile + "-" + std::to_string(j));
    }
}

INSTANTIATE_TEST_SUITE_P(ConvertTestData, ConvertTest1, ::testing::ValuesIn(g_testDataConvert));
#endif
