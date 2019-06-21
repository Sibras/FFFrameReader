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
#include "FFFRTestData.h"
#include "FFFRUtility.h"
#include "FFFrameReader.h"
#include "config.h"

#include <cuda.h>
#include <fstream>
#include <gtest/gtest.h>

extern "C" {
#include <libavutil/imgutils.h>
}

using namespace Ffr;

struct TestParamsConvert
{
    uint32_t m_testDataIndex;
    PixelFormat m_format;
    std::string m_imageFile;
};

static std::vector<TestParamsConvert> g_testDataConvert = {
#if BUILD_NPPI
    {1, PixelFormat::RGB8, "test1"},
    {1, PixelFormat::YUV420P, "test2"},
#endif
    {1, PixelFormat::RGB8P, "test3"},
    {1, PixelFormat::RGB32FP, "test4"},
};

class TestConvert
{
public:
    TestConvert() = default;

    void SetUp(const TestParamsConvert& params)
    {
        // Create a cuda context to be shared by decoder and this test
        ASSERT_EQ(cuInit(0), CUDA_SUCCESS);
        ASSERT_EQ(cuCtxGetCurrent(&m_context), CUDA_SUCCESS);
        if (m_context == nullptr) {
            ASSERT_EQ(cuDevicePrimaryCtxRetain(&m_context, 0), CUDA_SUCCESS);
        }

        DecoderOptions options;
        options.m_type = DecodeType::Cuda;
        options.m_outputHost = false;
        options.m_context = m_context;
        m_stream = Stream::getStream(g_testData[params.m_testDataIndex].m_fileName, options);
        ASSERT_NE(m_stream, nullptr);

        // Get frame dimensions
        const int width = m_stream->peekNextFrame()->getWidth();
        const int height = m_stream->peekNextFrame()->getHeight();

        // Allocate new memory to store frame data
        ASSERT_EQ(cuCtxPushCurrent(m_context), CUDA_SUCCESS);
        const auto outFrameSize = getImageSize(params.m_format, width, height);
        ASSERT_EQ(cuMemAlloc(&m_cudaBuffer, outFrameSize), CUDA_SUCCESS);
        CUcontext dummy;
        ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);
    }

    void TearDown()
    {
        m_stream.reset();
        if (reinterpret_cast<void*>(m_cudaBuffer) != nullptr) {
            ASSERT_EQ(cuCtxPushCurrent(m_context), CUDA_SUCCESS);
            ASSERT_EQ(cuMemFree(m_cudaBuffer), CUDA_SUCCESS);
            CUcontext dummy;
            ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);
        }
        if (m_context != nullptr) {
            cuDevicePrimaryCtxRelease(0);
        }
    }

    void saveImage(
        const PixelFormat format, const uint32_t width, const uint32_t height, const std::string& filename) const
    {
        if (format != PixelFormat::RGB32FP && format != PixelFormat::RGB8P && format != PixelFormat::RGB8) {
            return;
        }
        std::ofstream ofs;
        try {
            // Copy data to host
            std::vector<uint8_t> hostBuffer;
            const auto imageSize = getImageSize(format, width, height);
            hostBuffer.reserve(imageSize);
            ASSERT_EQ(cuCtxPushCurrent(m_context), CUDA_SUCCESS);
            ASSERT_EQ(cuMemcpyDtoH(hostBuffer.data(), m_cudaBuffer, imageSize), CUDA_SUCCESS);
            ASSERT_EQ(cuCtxSynchronize(), CUDA_SUCCESS);
            CUcontext dummy;
            ASSERT_EQ(cuCtxPopCurrent(&dummy), CUDA_SUCCESS);
            ofs.open(filename + ".ppm", std::ios::binary);
            ASSERT_FALSE(ofs.fail());
            ofs << "P6\n" << width << " " << height << "\n255\n";
            // Get each data frame
            uint8_t* outPlanes[4];
            int32_t outStep[4];
            av_image_fill_arrays(outPlanes, outStep, hostBuffer.data(), getPixelFormat(format), width, height, 32);
            // Loop over each pixel and output to file
            uint32_t offset = 0;
            for (uint32_t i = 0; i < height; ++i) {
                for (uint32_t j = 0; j < width; ++j) {
                    uint8_t r, g, b;
                    if (format == PixelFormat::RGB32FP) {
                        const auto jOffset = j * sizeof(float);
                        r = static_cast<uint8_t>(*reinterpret_cast<float*>(&(outPlanes[0][offset + jOffset])) * 255.0f);
                        g = static_cast<uint8_t>(*reinterpret_cast<float*>(&(outPlanes[1][offset + jOffset])) * 255.0f);
                        b = static_cast<uint8_t>(*reinterpret_cast<float*>(&(outPlanes[2][offset + jOffset])) * 255.0f);
                    } else if (format == PixelFormat::RGB8P) {
                        r = outPlanes[0][offset + j];
                        g = outPlanes[1][offset + j];
                        b = outPlanes[2][offset + j];
                    } else {
                        r = outPlanes[0][offset + (j * 3)];
                        g = outPlanes[0][offset + (j * 3) + 1];
                        b = outPlanes[0][offset + (j * 3) + 2];
                    }
                    ofs << r << g << b;
                }
                offset += outStep[0];
            }
            ofs.close();
        } catch (...) {
        }
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    CUcontext m_context = nullptr;
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