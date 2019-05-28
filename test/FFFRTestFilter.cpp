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

#include <gtest/gtest.h>

using namespace Ffr;

struct TestParamsDecode
{
    uint32_t m_testDataIndex;
    FfFrameReader::Resolution m_scale;
    FfFrameReader::Crop m_crop;
    FfFrameReader::PixelFormat m_format;
};

static std::vector<TestParamsDecode> g_testDataFilter = {
    {0, {1280, 720}, {0, 0, 0, 0}, FfFrameReader::PixelFormat::Auto},
    {0, {1280, 720}, {0, 360, 0, 640}, FfFrameReader::PixelFormat::Auto},
    {0, {1280, 720}, {180, 180, 320, 320}, FfFrameReader::PixelFormat::Auto},
    {0, {1920, 1080}, {0, 0, 0, 0}, FfFrameReader::PixelFormat::YUV444P},
};

class FilterTest1 : public ::testing::TestWithParam<TestParamsDecode>
{
protected:
    FilterTest1() = default;

    void SetUp() override
    {
        FfFrameReader::DecoderOptions options;
        options.m_scale = GetParam().m_scale;
        options.m_crop = GetParam().m_crop;
        options.m_format = GetParam().m_format;
        ASSERT_NO_THROW(m_frameReader = std::make_shared<FfFrameReader>());
        auto ret = m_frameReader->getStream(g_testData[GetParam().m_testDataIndex].m_fileName, options);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    void TearDown() override
    {
        m_stream = nullptr;
        m_frameReader = nullptr;
    }

    std::shared_ptr<FfFrameReader> m_frameReader = nullptr;
    std::shared_ptr<Stream> m_stream = nullptr;
    bool m_allocatorCalled = false;
};

TEST_P(FilterTest1, getWidth)
{
    ASSERT_EQ(m_stream->getWidth(), GetParam().m_scale.m_width);
    // Test the output frame matches
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame = std::get<1>(ret1);
    ASSERT_DOUBLE_EQ(frame->getWidth(), GetParam().m_scale.m_width);
}

TEST_P(FilterTest1, getHeight)
{
    ASSERT_EQ(m_stream->getHeight(), GetParam().m_scale.m_height);
    // Test the output frame matches
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame = std::get<1>(ret1);
    ASSERT_DOUBLE_EQ(frame->getHeight(), GetParam().m_scale.m_height);
}

TEST_P(FilterTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_stream->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
    // Test the output frame matches
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame = std::get<1>(ret1);
    ASSERT_DOUBLE_EQ(frame->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
}

TEST_P(FilterTest1, getFrameRate)
{
    ASSERT_DOUBLE_EQ(m_stream->getFrameRate(), g_testData[GetParam().m_testDataIndex].m_frameRate);
}

TEST_P(FilterTest1, getLoop25)
{
    // Ensure that all frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (int64_t i = 0; i < 25; i++) {
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
        ++frameNum;
    }
}
INSTANTIATE_TEST_SUITE_P(FilterTestData, FilterTest1, ::testing::ValuesIn(g_testDataFilter));