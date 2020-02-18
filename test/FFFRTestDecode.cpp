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
#include "FFFRTestData.h"
#include "FFFrameReader.h"

#if FFFR_BUILD_CUDA
#    include <cuda.h>
#endif
#include <gtest/gtest.h>

using namespace Ffr;

struct TestParamsDecode
{
    uint32_t m_testDataIndex;
    bool m_useNvdec;
    bool m_useContext;
    bool m_outputToHost;
    bool m_noBufferFlush;
    uint32_t m_bufferLength;
    bool m_resize;
};

static std::vector<TestParamsDecode> g_testDataDecode = {
    {0, false, false, true, false, 1, false},
    {1, false, false, true, false, 1, false},
    {2, false, false, true, false, 1, false},
    {0, true, false, false, false, 1, false},
    {0, true, false, true, false, 1, false},
    {0, true, false, false, false, 1, true},
    {0, true, false, true, false, 1, true},
#if FFFR_BUILD_CUDA
    {0, true, true, false, false, 1, false},
    {0, true, true, true, false, 1, false},
#endif
    {0, false, false, true, true, 1, false},
    {1, false, false, true, true, 1, false},
    {2, false, false, true, true, 1, false},
    {0, true, false, false, true, 1, false},
    {0, true, false, true, true, 1, false},
    {1, true, false, false, true, 1, false},
    {1, true, false, false, true, 10, false},
    {2, true, false, false, true, 1, false},
    {2, true, false, false, true, 10, false},
    {1, true, false, false, true, 1, true},
    {2, true, false, false, true, 10, true},
#if FFFR_INTERNAL_FILES
    {8, true, false, false, true, 1, false},
    {8, true, false, false, true, 10, false},
    {9, true, false, false, true, 1, false},
    {9, true, false, false, true, 10, false},
    {8, true, false, false, true, 1, true},
    {8, true, false, false, true, 10, true},
    {9, true, false, false, true, 1, true},
    {9, true, false, false, true, 10, true},
#endif
#if FFFR_BUILD_CUDA
    {0, true, true, false, true, 1, false},
    {0, true, true, true, true, 1, false},
    {0, true, true, false, true, 10, false},
    {0, true, true, true, true, 10, false},
#endif
};

class TestDecoder
{
public:
    TestDecoder() = default;

    ~TestDecoder()
    {
        m_stream.reset();
#if FFFR_BUILD_CUDA
        if (m_cudaContext != nullptr) {
            cuCtxDestroy(m_cudaContext);
        }
#endif
    }

    void SetUp(const TestParamsDecode& params)
    {
        DecoderOptions options;
        if (params.m_useNvdec) {
            options.m_type = DecodeType::Cuda;
#if FFFR_BUILD_CUDA
            if (params.m_useContext) {
                // Setup a cuda context
                auto err = cuInit(0);
                ASSERT_EQ(err, CUDA_SUCCESS);
                CUdevice device;
                err = cuDeviceGet(&device, 0);
                ASSERT_EQ(err, CUDA_SUCCESS);
                err = cuCtxCreate(&m_cudaContext, CU_CTX_SCHED_BLOCKING_SYNC, device);
                ASSERT_EQ(err, CUDA_SUCCESS);

                options.m_context = m_cudaContext;
            }
#endif
            if (params.m_resize) {
                options.m_scale = {1280, 720};
            }
        }
        options.m_outputHost = params.m_outputToHost;
        options.m_noBufferFlush = params.m_noBufferFlush;
        options.m_bufferLength = params.m_bufferLength;
        m_stream = Stream::getStream(g_testData[params.m_testDataIndex].m_fileName, options);
        ASSERT_NE(m_stream, nullptr);
    }

    void TearDown()
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
#if FFFR_BUILD_CUDA
    CUcontext m_cudaContext = nullptr;
#endif
};

class DecodeTest1 : public ::testing::TestWithParam<TestParamsDecode>
{
protected:
    DecodeTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Warning);
        m_decoder.SetUp(GetParam());
    }

    void TearDown() override
    {
        m_decoder.TearDown();
    }

    TestDecoder m_decoder;
};

TEST_P(DecodeTest1, getDecodeType)
{
    const auto frame1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    if (GetParam().m_useNvdec && !GetParam().m_outputToHost) {
        ASSERT_EQ(frame1->getDataType(), Ffr::DecodeType::Cuda);
    } else {
        ASSERT_EQ(frame1->getDataType(), Ffr::DecodeType::Software);
    }

    if (GetParam().m_useNvdec) {
        ASSERT_EQ(m_decoder.m_stream->getDecodeType(), Ffr::DecodeType::Cuda);
    } else {
        ASSERT_EQ(m_decoder.m_stream->getDecodeType(), Ffr::DecodeType::Software);
    }
}

TEST_P(DecodeTest1, getPixelFormat)
{
    const auto frame1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    if (GetParam().m_useNvdec) {
        ASSERT_EQ(frame1->getPixelFormat(), Ffr::PixelFormat::NV12);
    } else {
        ASSERT_EQ(frame1->getPixelFormat(), g_testData[GetParam().m_testDataIndex].m_format);
    }
}

TEST_P(DecodeTest1, getLoop25)
{
    // Ensure that frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (int64_t i = 0; i < std::min(m_decoder.m_stream->getTotalFrames(), 25LL); i++) {
        const auto frame1 = m_decoder.m_stream->getNextFrame();
        if (frame1 == nullptr) {
            ASSERT_EQ(timeStamp, m_decoder.m_stream->getDuration()); // Readout in case it failed
            ASSERT_EQ(i, m_decoder.m_stream->getTotalFrames());
        }
        ASSERT_NE(frame1, nullptr);
        ASSERT_EQ(frame1->getTimeStamp(), timeStamp);
        const double timeStamp1 =
            (static_cast<double>(i + 1) * (1000000.0 / g_testData[GetParam().m_testDataIndex].m_frameRate));
        timeStamp = llround(timeStamp1);
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}

TEST_P(DecodeTest1, getMultiple)
{
    // Create additional streams
    TestDecoder test2;
    test2.SetUp(GetParam());
    TestDecoder test3;
    test3.SetUp(GetParam());
    const auto frame1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    const auto frame2 = test2.m_stream->getNextFrame();
    ASSERT_NE(frame2, nullptr);
    const auto frame3 = test3.m_stream->getNextFrame();
    ASSERT_NE(frame3, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), 0);
    ASSERT_EQ(frame2->getTimeStamp(), 0);
    ASSERT_EQ(frame3->getTimeStamp(), 0);
}

TEST_P(DecodeTest1, seekFrame1Loop)
{
    // Seek to start
    ASSERT_TRUE(m_decoder.m_stream->seekFrame(0));
    ASSERT_NE(m_decoder.m_stream->getNextFrame(), nullptr);
    constexpr uint32_t numLoops = 5;
    constexpr uint32_t numFrames = 25;
    int64_t frame = 0;
    // Perform multiple forward seeks
    for (uint32_t i = 0; i < numLoops; i++) {
        if (frame >= g_testData[GetParam().m_testDataIndex].m_totalFrames) {
            return;
        }
        ASSERT_TRUE(m_decoder.m_stream->seekFrame(frame));
        // Check that multiple sequential frames can be read
        int64_t frame2 = frame;
        for (uint32_t j = 0; j < numFrames; j++) {
            const auto frame1 = m_decoder.m_stream->getNextFrame();
            // Allow EOF
            if (frame1 == nullptr && m_decoder.m_stream->isEndOfFile()) {
                return;
            }
            ASSERT_NE(frame1, nullptr);
            ASSERT_EQ(frame1->getFrameNumber(), frame2);
            ++frame2;
        }
        frame += 1;
    }
}

TEST_P(DecodeTest1, seekFrame25Loop)
{
    // Seek to start
    ASSERT_TRUE(m_decoder.m_stream->seekFrame(0));
    ASSERT_NE(m_decoder.m_stream->getNextFrame(), nullptr);
    constexpr uint32_t numLoops = 5;
    constexpr uint32_t numFrames = 5;
    int64_t frame = 0;
    // Perform multiple forward seeks
    for (uint32_t i = 0; i < numLoops; i++) {
        if (frame >= g_testData[GetParam().m_testDataIndex].m_totalFrames) {
            return;
        }
        ASSERT_TRUE(m_decoder.m_stream->seekFrame(frame));
        // Check that multiple sequential frames can be read
        int64_t frame2 = frame;
        for (uint32_t j = 0; j < numFrames; j++) {
            const auto frame1 = m_decoder.m_stream->getNextFrame();
            // Allow EOF
            if (frame1 == nullptr && m_decoder.m_stream->isEndOfFile()) {
                return;
            }
            ASSERT_NE(frame1, nullptr);
            ASSERT_EQ(frame1->getFrameNumber(), frame2);
            ++frame2;
        }
        frame += 25;
    }
}

INSTANTIATE_TEST_SUITE_P(DecodeTestData, DecodeTest1, ::testing::ValuesIn(g_testDataDecode));
