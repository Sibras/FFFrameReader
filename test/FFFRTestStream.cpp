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
#include "FfFrameReader.h"
#include "gtest/gtest.h"

using namespace FfFrameReader;

const std::string g_test1File = "data/Men_50BR_FinalA_Canon.MP4";

class StreamTest1 : public ::testing::Test
{
protected:
    StreamTest1() = default;

    void SetUp() override
    {
        const auto ret = Interface::getStream(g_test1File);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    ~StreamTest1() override
    {
        Interface::releaseStream(g_test1File);
    }

    StreamInterface m_stream;
};

TEST_F(StreamTest1, getWidth)
{
    ASSERT_EQ(m_stream.getWidth(), 3840);
}

TEST_F(StreamTest1, getHeight)
{
    ASSERT_EQ(m_stream.getHeight(), 2160);
}

TEST_F(StreamTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_stream.getAspectRatio(), 16.0 / 9.0);
}

TEST_F(StreamTest1, getTotalFrames)
{
    ASSERT_EQ(m_stream.getTotalFrames(), 1296);
}

TEST_F(StreamTest1, getDuration)
{
    ASSERT_EQ(m_stream.getDuration(), 2073600000);
}

TEST_F(StreamTest1, getNextFrame)
{
    ASSERT_NE(m_stream.getNextFrame().index(), 0);
}

TEST_F(StreamTest1, getNextFrame2)
{
    const auto ret1 = m_stream.getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto ret2 = m_stream.getNextFrame();
    ASSERT_NE(ret2.index(), 0);
    // TODO: Check that the frames are in fact different
}

TEST_F(StreamTest1, getNextFrameLoop)
{
    // Ensure that multiple frames can be read
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream.getNextFrame();
        ASSERT_NE(ret1.index(), 0);
    }
}

class FrameTest1 : public ::testing::Test
{
protected:
    FrameTest1() = default;

    void SetUp() override
    {
        const auto ret = Interface::getStream(g_test1File);
        ASSERT_NE(ret.index(), 0);
        const auto stream = std::get<1>(ret);
        const auto ret1 = stream.getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        m_frame = std::get<1>(ret1);
    }

    ~FrameTest1() override
    {
        Interface::releaseStream(g_test1File);
    }

    FrameInterface m_frame;
};

TEST_F(FrameTest1, getTimeStamp)
{
    ASSERT_EQ(m_frame.getTimeStamp(), 0);
}

TEST_F(FrameTest1, getFrameNumber)
{
    ASSERT_EQ(m_frame.getFrameNumber(), 0);
}

TEST_F(FrameTest1, getWidth)
{
    ASSERT_EQ(m_frame.getWidth(), 3840);
}

TEST_F(FrameTest1, getHeight)
{
    ASSERT_EQ(m_frame.getHeight(), 2160);
}

TEST_F(FrameTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_frame.getAspectRatio(), 16.0 / 9.0);
}

TEST_F(StreamTest1, getTimeStampLoop)
{
    // Ensure that multiple frames can be read
    int64_t timeStamp = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream.getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1.getTimeStamp(), timeStamp);
        timeStamp += 40000;
    }
}

TEST_F(StreamTest1, getFrameNumberLoop)
{
    // Ensure that multiple frames can be read
    int64_t frameNum = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream.getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1.getFrameNumber(), frameNum);
        ++frameNum;
    }
}

// TODO: seek test

// TODO: do all the same tests for each of the input data files