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

class StreamTest1 : public ::testing::TestWithParam<TestParams>
{
protected:
    StreamTest1() = default;

    void SetUp() override
    {
        setLogLevel(LogLevel::Warning);
        m_stream = Stream::getStream(GetParam().m_fileName);
        ASSERT_NE(m_stream, nullptr);
    }

    void TearDown() override
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
};

TEST_P(StreamTest1, getWidth)
{
    ASSERT_EQ(m_stream->getWidth(), GetParam().m_width);
}

TEST_P(StreamTest1, getHeight)
{
    ASSERT_EQ(m_stream->getHeight(), GetParam().m_height);
}

TEST_P(StreamTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_stream->getAspectRatio(), GetParam().m_aspectRatio);
}

TEST_P(StreamTest1, getTotalFrames)
{
    ASSERT_EQ(m_stream->getTotalFrames(), GetParam().m_totalFrames);
}

TEST_P(StreamTest1, getDuration)
{
    ASSERT_EQ(m_stream->getDuration(), GetParam().m_duration);
}

TEST_P(StreamTest1, getFrameRate)
{
    ASSERT_DOUBLE_EQ(m_stream->getFrameRate(), GetParam().m_frameRate);
}

TEST_P(StreamTest1, getPixelFormat)
{
    ASSERT_EQ(m_stream->getPixelFormat(), GetParam().m_format);
}

INSTANTIATE_TEST_SUITE_P(StreamTestData, StreamTest1, ::testing::ValuesIn(g_testData));
