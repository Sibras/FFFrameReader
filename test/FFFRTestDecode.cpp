﻿/**
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
#include "FfFrameReader.h"

#include <cuda.h>
#include <gtest/gtest.h>

using namespace FfFrameReader;

struct TestParamsDecode
{
    uint32_t m_testDataIndex;
    bool m_useNvdec;
    bool m_useContext;
};

static std::vector<TestParamsDecode> g_testDataDecode = {
    //{0, false, false},
    {0, true, false},
    //{0, true, true},
};

class DecodeTest1 : public ::testing::TestWithParam<TestParamsDecode>
{
protected:
    DecodeTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Error);

        DecoderContext::DecoderOptions options;
        if (GetParam().m_useNvdec) {
            options.m_type = DecoderContext::DecodeType::Nvdec;
            // Setup a cuda context
            auto err = cuInit(0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            CUdevice device;
            err = cuDeviceGet(&device, 0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            if (GetParam().m_useContext) {
                err = cuCtxCreate(&m_cudaContext, CU_CTX_SCHED_BLOCKING_SYNC, device);
                ASSERT_EQ(err, CUDA_SUCCESS);

                options.m_context = m_cudaContext;
            } else {
                // Use default cuda context
                err = cuDevicePrimaryCtxRetain(&m_cudaContext, 0);
                ASSERT_EQ(err, CUDA_SUCCESS);
            }
        }
        ASSERT_NO_THROW(m_context = std::make_shared<DecoderContext>(options));
        auto ret = m_context->getStream(g_testData[GetParam().m_testDataIndex].m_fileName);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    void TearDown() override
    {
        m_stream = nullptr;
        m_context = nullptr;
    }

    ~DecodeTest1() override
    {
        if (m_cudaContext != nullptr) {
            cuCtxDestroy(m_cudaContext);
        }
    }

    std::shared_ptr<DecoderContext> m_context = nullptr;
    std::shared_ptr<Stream> m_stream = nullptr;
    CUcontext m_cudaContext = nullptr;
    bool m_allocatorCalled = false;
};

TEST_P(DecodeTest1, getLoopAll)
{
    // Ensure that all frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (int64_t i = 0; i < m_stream->getTotalFrames(); i++) {
        const auto ret1 = m_stream->getNextFrame();
        if (ret1.index() == 0) {
            ASSERT_EQ(timeStamp, m_stream->getDuration()); // Readout in case it failed
            ASSERT_EQ(i, m_stream->getTotalFrames());
        }
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1->getTimeStamp(), timeStamp);
        const double timeStamp1 =
            (static_cast<double>(i + 1) * (1000000.0 / g_testData[GetParam().m_testDataIndex].m_frameRate));
        timeStamp = llround(timeStamp1);
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        printf("  frame0: %p\n", frame1->getFrameData()[0]);
        printf("  frame1: %p\n", frame1->getFrameData()[1]);
        printf("  frame2: %p\n", frame1->getFrameData()[2]);
        ++frameNum;
    }
}
INSTANTIATE_TEST_SUITE_P(DecodeTestData, DecodeTest1, ::testing::ValuesIn(g_testDataDecode));