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
#include "FFFREncoder.h"
#include "FFFRTestData.h"
#include "FFFrameReader.h"

#include <gtest/gtest.h>

using namespace Ffr;

struct TestParamsEncode
{
    uint32_t m_testDataIndex;
    std::string m_fileName;
    EncodeType m_encodeType;
    uint8_t m_quality;
    EncoderOptions::Preset m_preset;
    bool m_useFiltering;
    bool m_useGOP;
};

static std::vector<TestParamsEncode> g_testDataEncode = {
    {1, "test01.mp4", EncodeType::h264, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {1, "test02.mp4", EncodeType::h265, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {1, "test03.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast, false, false},
    {1, "test04.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast, false, false},
    {1, "test05.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast, true, true},
    {2, "test06.mp4", EncodeType::h264, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {2, "test07.mp4", EncodeType::h265, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {2, "test08.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast, false, false},
    {2, "test09.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast, false, false},
    {2, "test10.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast, true, true},
    {3, "test11.mp4", EncodeType::h264, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {3, "test12.mp4", EncodeType::h265, 125, EncoderOptions::Preset::Ultrafast, false, false},
    {3, "test13.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast, false, false},
    {3, "test14.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast, false, false},
    {3, "test15.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast, true, true},
};

class TestEncoder
{
public:
    TestEncoder() = default;

    void SetUp(const TestParamsEncode& params)
    {
        DecoderOptions options;
        if (params.m_useFiltering) {
            options.m_scale = {640, 360};
        }
        m_stream = Stream::getStream(g_testData[params.m_testDataIndex].m_fileName, options);
        ASSERT_NE(m_stream, nullptr);
    }

    void TearDown()
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
};

class EncodeTest1 : public ::testing::TestWithParam<TestParamsEncode>
{
protected:
    EncodeTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Error);
        m_decoder.SetUp(GetParam());
    }

    void TearDown() override
    {
        m_decoder.TearDown();
    }

    TestEncoder m_decoder;
};

TEST_P(EncodeTest1, encodeStream)
{
    EncoderOptions options2;
    options2.m_type = GetParam().m_encodeType;
    options2.m_quality = GetParam().m_quality;
    options2.m_preset = GetParam().m_preset;
    if (GetParam().m_useGOP) {
        options2.m_gopSize = 3;
    }

    // Just run an encode and see if output is correct manually
    ASSERT_TRUE(Encoder::encodeStream(GetParam().m_fileName, m_decoder.m_stream, options2));

    // Check that we can open encoded file and its parameters are correct
    auto stream = Stream::getStream(GetParam().m_fileName);
    ASSERT_NE(stream, nullptr);

    ASSERT_EQ(stream->getWidth(), GetParam().m_useFiltering ? 640 : g_testData[GetParam().m_testDataIndex].m_width);
    ASSERT_EQ(stream->getHeight(), GetParam().m_useFiltering ? 360 : g_testData[GetParam().m_testDataIndex].m_height);
    ASSERT_DOUBLE_EQ(stream->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
    ASSERT_EQ(stream->getTotalFrames(), g_testData[GetParam().m_testDataIndex].m_totalFrames);
    ASSERT_DOUBLE_EQ(stream->getFrameRate(), g_testData[GetParam().m_testDataIndex].m_frameRate);
    ASSERT_EQ(stream->getDuration(), g_testData[GetParam().m_testDataIndex].m_duration);
}

INSTANTIATE_TEST_SUITE_P(EncodeTestData, EncodeTest1, ::testing::ValuesIn(g_testDataEncode));
