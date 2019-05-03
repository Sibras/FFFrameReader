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
#include "FfFrameReader.h"

#include <cuda.h>
#include <gtest/gtest.h>
using namespace FfFrameReader;

struct TestParamsNVDec : TestParams
{
    bool m_useContext;
    bool m_useAllocator;
};

static std::vector<TestParamsNVDec> g_testDataNVDec = {
    {{"data/bbb_sunflower_1080p_30fps_normal.mp4", 1920, 1080, 16.0 / 9.0, 19034, 634466666, 30.0, 33333}, false,
        false},
    //{{"data/bbb_sunflower_1080p_30fps_normal.mp4", 1920, 1080, 16.0 / 9.0, 19034, 634466666, 30.0, 33333}, false,
    // true},
    //{{"data/bbb_sunflower_1080p_30fps_normal.mp4", 1920, 1080, 16.0 / 9.0, 19034, 634466666, 30.0, 33333}, true,
    // false},
    // {{"data/bbb_sunflower_1080p_30fps_normal.mp4", 1920, 1080, 16.0 / 9.0, 19034, 634466666, 30.0, 33333}, true,
    // true},
    ////currently broken
};

class NVDecTest1 : public ::testing::TestWithParam<TestParamsNVDec>
{
protected:
    NVDecTest1() = default;

    void SetUp() override
    {
        DecoderContext::DecoderOptions options(DecoderContext::DecodeType::Nvdec);
        if (GetParam().m_useContext) {
            // Create a cuda context
            auto err = cuInit(0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            CUdevice device;
            err = cuDeviceGet(&device, 0);
            ASSERT_EQ(err, CUDA_SUCCESS);
            err = cuCtxCreate(&m_cudaContext, CU_CTX_SCHED_BLOCKING_SYNC, device);
            ASSERT_EQ(err, CUDA_SUCCESS);

            options.m_context = m_cudaContext;
        }
        if (GetParam().m_useAllocator) {
            const std::function<uint8_t*(uint32_t)> allocator =
                std::bind(&NVDecTest1::allocateCuda, this, std::placeholders::_1);
            const std::function<void(uint8_t*)> free = std::bind(&NVDecTest1::freeCuda, this, std::placeholders::_1);
            options.m_allocator = std::optional<DecoderContext::DecoderOptions::Allocator>({allocator, free});
        }
        ASSERT_NO_THROW(m_context = std::make_shared<DecoderContext>(options));
        auto ret = m_context->getStream(GetParam().m_fileName);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    void TearDown() override
    {
        m_stream = nullptr;
        m_context = nullptr;
        ASSERT_EQ(m_allocateNum, m_freeNum);
        if (GetParam().m_useAllocator) {
            ASSERT_TRUE(m_allocatorCalled);
            ASSERT_GT(m_allocateNum, 0UL);
        }
    }

    ~NVDecTest1() override
    {
        if (m_cudaContext != nullptr) {
            cuCtxDestroy(m_cudaContext);
        }
    }

    uint8_t* allocateCuda(const uint32_t size)
    {
        m_allocatorCalled = true;
        CUcontext dummy = nullptr;
        auto err = cuCtxPushCurrent(m_cudaContext);
        if (err < 0) {
            return nullptr;
        }
        CUdeviceptr data;
        uint8_t* ret = nullptr;
        err = cuMemAlloc(&data, size);
        if (err >= 0) {
            ret = reinterpret_cast<uint8_t*>(data);
        }

        cuCtxPopCurrent(&dummy);
        ++m_allocateNum;
        return ret;
    }

    void freeCuda(uint8_t* data)
    {
        CUcontext dummy;
        cuCtxPushCurrent(m_cudaContext);
        cuMemFree(reinterpret_cast<CUdeviceptr>(data));
        cuCtxPopCurrent(&dummy);
        ++m_freeNum;
    }

    std::shared_ptr<DecoderContext> m_context = nullptr;
    std::shared_ptr<Stream> m_stream = nullptr;
    CUcontext m_cudaContext = nullptr;
    bool m_allocatorCalled = false;
    uint32_t m_allocateNum = 0;
    uint32_t m_freeNum = 0;
};

TEST_P(NVDecTest1, getNextFrame)
{
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
}

TEST_P(NVDecTest1, getNextFrame2)
{
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto ret2 = m_stream->getNextFrame();
    ASSERT_NE(ret2.index(), 0);
}

TEST_P(NVDecTest1, getNextFrameLoop)
{
    // Ensure that multiple frames can be read
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
    }
}

TEST_P(NVDecTest1, getLoop)
{
    // Ensure that multiple frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1->getTimeStamp(), timeStamp);
        const double timeStamp1 = (static_cast<double>(i + 1) * (1000000.0 / GetParam().m_frameRate));
        timeStamp = llround(timeStamp1);
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}

TEST_P(NVDecTest1, getLoopAll)
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
        const double timeStamp1 = (static_cast<double>(i + 1) * (1000000.0 / GetParam().m_frameRate));
        timeStamp = llround(timeStamp1);
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}
INSTANTIATE_TEST_SUITE_P(NVDecTestData, NVDecTest1, ::testing::ValuesIn(g_testDataNVDec));