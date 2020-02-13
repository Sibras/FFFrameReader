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

extern void saveImage(PixelFormat format, uint32_t width, uint32_t height, const std::string& filename,
    uint8_t* buffer[4], int32_t step[4]) noexcept;

struct TestParamsFilter
{
    uint32_t m_testDataIndex;
    DecodeType m_type;
    Resolution m_scale;
    Crop m_crop;
    PixelFormat m_format;
    std::string m_imageFile;
};

static std::vector<TestParamsFilter> g_testDataFilter = {
    {0, DecodeType::Software, {1280, 720}, {0, 0, 0, 0}, PixelFormat::Auto, "test-filter-1"},
    {0, DecodeType::Software, {1280, 720}, {0, 360, 0, 640}, PixelFormat::Auto, "test-filter-2"},
    {0, DecodeType::Software, {1280, 720}, {180, 180, 320, 320}, PixelFormat::Auto, "test-filter-3"},
    {0, DecodeType::Software, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::YUV422P, "test-filter-4"},
    {0, DecodeType::Software, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB8, "test-filter-5"},
    {0, DecodeType::Software, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB8P, "test-filter-6"},
    //{0, DecodeType::Software, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB32FP, "test-filter-7"},
    {0, DecodeType::Cuda, {1280, 720}, {0, 0, 0, 0}, PixelFormat::Auto, "test-filter-8"},
    {0, DecodeType::Cuda, {1280, 720}, {0, 360, 0, 640}, PixelFormat::Auto, "test-filter-9"},
    {0, DecodeType::Cuda, {1280, 720}, {180, 180, 320, 320}, PixelFormat::Auto, "test-filter-10"},
    //{0, DecodeType::Cuda, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::YUV422P, "test-filter-11"},
    //{0, DecodeType::Cuda, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB8, "test-filter-12"},
    //{0, DecodeType::Cuda, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB8P, "test-filter-13"},
    //{0, DecodeType::Software, {1920, 1080}, {0, 0, 0, 0}, PixelFormat::RGB32FP, "test-filter-14"},
};

class FilterTest1 : public ::testing::TestWithParam<TestParamsFilter>
{
protected:
    FilterTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Warning);
        DecoderOptions options;
        options.m_type = GetParam().m_type;
        options.m_scale = GetParam().m_scale;
        options.m_crop = GetParam().m_crop;
        options.m_format = GetParam().m_format;
        options.m_outputHost = false;
        m_stream = Stream::getStream(g_testData[GetParam().m_testDataIndex].m_fileName, options);
        ASSERT_NE(m_stream, nullptr);
    }

    void TearDown() override
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    bool m_allocatorCalled = false;
};

TEST_P(FilterTest1, getWidth)
{
    ASSERT_EQ(m_stream->getWidth(), GetParam().m_scale.m_width);
    // Test the output frame matches
    const auto frame = m_stream->getNextFrame();
    ASSERT_NE(frame, nullptr);
    ASSERT_DOUBLE_EQ(frame->getWidth(), GetParam().m_scale.m_width);
}

TEST_P(FilterTest1, getHeight)
{
    ASSERT_EQ(m_stream->getHeight(), GetParam().m_scale.m_height);
    // Test the output frame matches
    const auto frame = m_stream->getNextFrame();
    ASSERT_NE(frame, nullptr);
    ASSERT_DOUBLE_EQ(frame->getHeight(), GetParam().m_scale.m_height);
}

TEST_P(FilterTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_stream->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
    // Test the output frame matches
    const auto frame = m_stream->getNextFrame();
    ASSERT_NE(frame, nullptr);
    ASSERT_DOUBLE_EQ(frame->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
}

TEST_P(FilterTest1, getFrameRate)
{
    ASSERT_DOUBLE_EQ(m_stream->getFrameRate(), g_testData[GetParam().m_testDataIndex].m_frameRate);
}

TEST_P(FilterTest1, getFormat)
{
    const auto frame = m_stream->getNextFrame();
    ASSERT_NE(frame, nullptr);
    const auto format = GetParam().m_format == PixelFormat::Auto ?
        GetParam().m_type == DecodeType::Cuda ? PixelFormat::NV12 : PixelFormat::YUV420P :
        GetParam().m_format;
    ASSERT_EQ(frame->getPixelFormat(), format);
}

TEST_P(FilterTest1, getLoop25)
{
    // Ensure that all frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (int64_t i = 0; i < std::min(m_stream->getTotalFrames(), 25LL); i++) {
        const auto frame1 = m_stream->getNextFrame();
        if (frame1 == nullptr) {
            ASSERT_EQ(timeStamp, m_stream->getDuration()); // Readout in case it failed
            ASSERT_EQ(i, m_stream->getTotalFrames());
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

TEST_P(FilterTest1, output)
{
    if (GetParam().m_format == PixelFormat::RGB32FP || GetParam().m_format == PixelFormat::RGB8P ||
        GetParam().m_format == PixelFormat::RGB8) {
        ASSERT_TRUE(m_stream->seekFrame(m_stream->getTotalFrames() / 2));
        const auto frame1 = m_stream->getNextFrame();
        ASSERT_NE(frame1, nullptr);

        // Check if known pixel format
        ASSERT_NE(frame1->getPixelFormat(), Ffr::PixelFormat::Auto);

        // Save to image for visual inspection
        uint8_t* outPlanes[4];
        int32_t outStep[4];
        outPlanes[0] = frame1->getFrameData(0).first;
        outStep[0] = frame1->getFrameData(0).second;
        for (int32_t i = 1; i < getPixelFormatPlanes(frame1->getPixelFormat()); i++) {
            outPlanes[i] = frame1->getFrameData(i).first;
            outStep[i] = frame1->getFrameData(i).second;
        }
        saveImage(
            GetParam().m_format, frame1->getWidth(), frame1->getHeight(), GetParam().m_imageFile, outPlanes, outStep);
    }
}

INSTANTIATE_TEST_SUITE_P(FilterTestData, FilterTest1, ::testing::ValuesIn(g_testDataFilter));
