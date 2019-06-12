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
};

static std::vector<TestParamsEncode> g_testDataEncode = {
    {1, "test1.mp4", EncodeType::h264, 125, EncoderOptions::Preset::Ultrafast},
    {1, "test2.mp4", EncodeType::h265, 125, EncoderOptions::Preset::Ultrafast},
    {1, "test3.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast},
    {1, "test4.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast},
    {2, "test5.mp4", EncodeType::h264, 125, EncoderOptions::Preset::Ultrafast},
    {2, "test6.mp4", EncodeType::h265, 125, EncoderOptions::Preset::Ultrafast},
    {2, "test7.mp4", EncodeType::h264, 55, EncoderOptions::Preset::Veryfast},
    {2, "test8.mp4", EncodeType::h265, 55, EncoderOptions::Preset::Veryfast},
};

class TestDecoder
{
public:
    TestDecoder() = default;

    ~TestDecoder()
    {
        m_stream = nullptr;
    }

    void SetUp(const TestParamsEncode& params)
    {
        DecoderOptions options;
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
        setLogLevel(LogLevel::Warning);
        m_decoder.SetUp(GetParam());
    }

    void TearDown() override
    {
        m_decoder.TearDown();
    }

    TestDecoder m_decoder;
};

TEST_P(EncodeTest1, encodeStream)
{
    EncoderOptions options2;
    options2.m_type = GetParam().m_encodeType;
    options2.m_quality = GetParam().m_quality;
    options2.m_preset = GetParam().m_preset;

    // Just run an encode and see if output is correct manually
    ASSERT_TRUE(Encoder::encodeStream(GetParam().m_fileName, m_decoder.m_stream, options2));

    // Check that we can open encoded file and its parameters are correct
    auto stream = Stream::getStream(GetParam().m_fileName);
    ASSERT_NE(stream, nullptr);

    ASSERT_EQ(stream->getWidth(), g_testData[GetParam().m_testDataIndex].m_width);
    ASSERT_EQ(stream->getHeight(), g_testData[GetParam().m_testDataIndex].m_height);
    ASSERT_DOUBLE_EQ(stream->getAspectRatio(), g_testData[GetParam().m_testDataIndex].m_aspectRatio);
    ASSERT_EQ(stream->getTotalFrames(), g_testData[GetParam().m_testDataIndex].m_totalFrames);
    ASSERT_EQ(stream->getDuration(), g_testData[GetParam().m_testDataIndex].m_duration);
    ASSERT_DOUBLE_EQ(stream->getFrameRate(), g_testData[GetParam().m_testDataIndex].m_frameRate);
}

INSTANTIATE_TEST_SUITE_P(EncodeTestData, EncodeTest1, ::testing::ValuesIn(g_testDataEncode));