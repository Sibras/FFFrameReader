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
#include "FFFrameReader.h"

#include <cuda.h>
#include <gtest/gtest.h>

using namespace Ffr;

struct TestParamsDecode
{
    uint32_t m_testDataIndex;
    bool m_useNvdec;
    bool m_useContext;
    bool m_outputToHost;
};

static std::vector<TestParamsDecode> g_testDataDecode = {
    {0, false, false, true},
    {0, true, false, false},
    {0, true, false, true},
    {0, true, true, false},
    {0, true, true, true},
};

class TestDecoder
{
public:
    TestDecoder() = default;

    ~TestDecoder()
    {
        m_stream = nullptr;
        if (m_cudaContext != nullptr) {
            cuCtxDestroy(m_cudaContext);
        }
    }

    void SetUp(const TestParamsDecode& params)
    {
        DecoderOptions options;
        if (params.m_useNvdec) {
            options.m_type = DecodeType::Cuda;
            // Setup a cuda context
            auto err = cuInit(0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            CUdevice device;
            err = cuDeviceGet(&device, 0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            if (params.m_useContext) {
                err = cuCtxCreate(&m_cudaContext, CU_CTX_SCHED_BLOCKING_SYNC, device);
                ASSERT_EQ(err, CUDA_SUCCESS);

                options.m_context = m_cudaContext;
            } else {
                // Use default cuda context
                err = cuDevicePrimaryCtxRetain(&m_cudaContext, 0);
                ASSERT_EQ(err, CUDA_SUCCESS);
            }
        }
        options.m_outputHost = params.m_outputToHost;
        auto ret = Stream::getStream(g_testData[params.m_testDataIndex].m_fileName, options);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    void TearDown()
    {
        m_stream = nullptr;
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    CUcontext m_cudaContext = nullptr;
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
    const auto ret1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    if (GetParam().m_useNvdec && !GetParam().m_outputToHost) {
        ASSERT_EQ(frame1->getDataType(), Ffr::DecodeType::Cuda);
    } else {
        ASSERT_EQ(frame1->getDataType(), Ffr::DecodeType::Software);
    }
}

TEST_P(DecodeTest1, getPixelFormat)
{
    const auto ret1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getPixelFormat(), g_testData[GetParam().m_testDataIndex].m_format);
}

TEST_P(DecodeTest1, getLoop25)
{
    // Ensure that frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (int64_t i = 0; i < std::min(m_decoder.m_stream->getTotalFrames(), 25LL); i++) {
        const auto ret1 = m_decoder.m_stream->getNextFrame();
        if (ret1.index() == 0) {
            ASSERT_EQ(timeStamp, m_decoder.m_stream->getDuration()); // Readout in case it failed
            ASSERT_EQ(i, m_decoder.m_stream->getTotalFrames());
        }
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
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
    const auto ret1 = m_decoder.m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    const auto ret2 = test2.m_stream->getNextFrame();
    ASSERT_NE(ret2.index(), 0);
    const auto frame2 = std::get<1>(ret2);
    const auto ret3 = test3.m_stream->getNextFrame();
    ASSERT_NE(ret3.index(), 0);
    const auto frame3 = std::get<1>(ret3);
    ASSERT_EQ(frame1->getTimeStamp(), 0);
    ASSERT_EQ(frame2->getTimeStamp(), 0);
    ASSERT_EQ(frame3->getTimeStamp(), 0);
}

INSTANTIATE_TEST_SUITE_P(DecodeTestData, DecodeTest1, ::testing::ValuesIn(g_testDataDecode));