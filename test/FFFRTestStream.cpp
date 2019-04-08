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
        auto ret = m_manager.getStream(g_test1File);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    ~StreamTest1() override
    {
        m_manager.releaseStream(g_test1File);
    }

    Manager m_manager;
    std::shared_ptr<Stream> m_stream;
};

TEST_F(StreamTest1, getWidth)
{
    ASSERT_EQ(m_stream->getWidth(), 3840);
}

TEST_F(StreamTest1, getHeight)
{
    ASSERT_EQ(m_stream->getHeight(), 2160);
}

TEST_F(StreamTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_stream->getAspectRatio(), 16.0 / 9.0);
}

TEST_F(StreamTest1, getTotalFrames)
{
    ASSERT_EQ(m_stream->getTotalFrames(), 1296);
}

TEST_F(StreamTest1, getDuration)
{
    ASSERT_EQ(m_stream->getDuration(), 2073600000);
}

TEST_F(StreamTest1, getFrameRate)
{
    ASSERT_DOUBLE_EQ(m_stream->getFrameRate(), 25.0);
}

TEST_F(StreamTest1, getNextFrame)
{
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
}

TEST_F(StreamTest1, getNextFrame2)
{
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto ret2 = m_stream->getNextFrame();
    ASSERT_NE(ret2.index(), 0);
    // TODO: Check that the frames are in fact different
}

TEST_F(StreamTest1, getNextFrameLoop)
{
    // Ensure that multiple frames can be read
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
    }
}

class FrameTest1 : public ::testing::Test
{
protected:
    FrameTest1() = default;

    void SetUp() override
    {
        const auto ret = m_manager.getStream(g_test1File);
        ASSERT_NE(ret.index(), 0);
        const auto stream = std::get<1>(ret);
        const auto ret1 = stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        m_frame = std::get<1>(ret1);
    }

    ~FrameTest1() override
    {
        m_manager.releaseStream(g_test1File);
    }

    Manager m_manager;
    std::shared_ptr<Frame> m_frame;
};

TEST_F(FrameTest1, getTimeStamp)
{
    ASSERT_EQ(m_frame->getTimeStamp(), 0);
}

TEST_F(FrameTest1, getFrameNumber)
{
    ASSERT_EQ(m_frame->getFrameNumber(), 0);
}

TEST_F(FrameTest1, getWidth)
{
    ASSERT_EQ(m_frame->getWidth(), 3840);
}

TEST_F(FrameTest1, getHeight)
{
    ASSERT_EQ(m_frame->getHeight(), 2160);
}

TEST_F(FrameTest1, getAspectRatio)
{
    ASSERT_DOUBLE_EQ(m_frame->getAspectRatio(), 16.0 / 9.0);
}

TEST_F(StreamTest1, getTimeStampLoop)
{
    // Ensure that multiple frames can be read
    int64_t timeStamp = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1->getTimeStamp(), timeStamp);
        timeStamp += 40000;
    }
}

TEST_F(StreamTest1, getFrameNumberLoop)
{
    // Ensure that multiple frames can be read
    int64_t frameNum = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}

TEST_F(StreamTest1, seek)
{
    ASSERT_TRUE(m_stream->seek(40000 * 80));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 40000 * 80);
}

TEST_F(StreamTest1, seekSmall)
{
    // First fill the buffer
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
    // Seek forward 2 frames only. This should just increment the existing buffer
    ASSERT_TRUE(m_stream->seek(40000 * 2));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 40000 * 2);
}

TEST_F(StreamTest1, seekMedium)
{
    // First fill the buffer
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
    // Seek forward 1.5 * bufferSize frame
    ASSERT_TRUE(m_stream->seek(40000 * 15));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 40000 * 15);
}

TEST_F(StreamTest1, seekLoop)
{
    int64_t timeStamp = 0;
    // Perform multiple forward seeks
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_TRUE(m_stream->seek(timeStamp));
        // Check that multiple sequential frames can be read
        int64_t timeStamp2 = timeStamp;
        for (uint32_t j = 0; j < 25; j++) {
            const auto ret1 = m_stream->getNextFrame();
            ASSERT_NE(ret1.index(), 0);
            const auto frame1 = std::get<1>(ret1);
            ASSERT_EQ(frame1->getTimeStamp(), timeStamp2);
            timeStamp2 += 40000;
        }
        timeStamp += 40000 * 40;
    }
}

TEST_F(StreamTest1, seekBack)
{
    // Seek forward
    ASSERT_TRUE(m_stream->seek(40000 * 80));
    auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 40000 * 80);
    // Seek back
    ASSERT_TRUE(m_stream->seek(0));
    ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 0);
}

TEST_F(StreamTest1, seekFrame)
{
    ASSERT_TRUE(m_stream->seekFrame(80));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getFrameNumber(), 80);
}

TEST_F(StreamTest1, seekFrameLoop)
{
    int64_t frame = 0;
    // Perform multiple forward seeks
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_TRUE(m_stream->seekFrame(frame));
        // Check that multiple sequential frames can be read
        int64_t frame2 = frame;
        for (uint32_t j = 0; j < 25; j++) {
            const auto ret1 = m_stream->getNextFrame();
            ASSERT_NE(ret1.index(), 0);
            const auto frame1 = std::get<1>(ret1);
            ASSERT_EQ(frame1->getFrameNumber(), frame2);
            ++frame2;
        }
        frame += 80;
    }
}

TEST_F(StreamTest1, getNextFrameSequence)
{
    const std::vector<int64_t> framesList1 = {0, 1, 5, 7, 8};
    const auto ret1 = m_stream->getNextFrameSequence(framesList1);
    ASSERT_NE(ret1.index(), 0);
    // Check that the returned frames are correct
    const auto frames1 = std::get<1>(ret1);
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getFrameNumber(), framesList1[j]);
        ++j;
    }
}

TEST_F(StreamTest1, getNextFrameSequenceSeek)
{
    // First seek to a frame
    ASSERT_TRUE(m_stream->seekFrame(80));
    // Now get frame sequence off set from current
    const std::vector<int64_t> framesList1 = {0, 1, 5, 7, 8};
    const auto ret1 = m_stream->getNextFrameSequence(framesList1);
    ASSERT_NE(ret1.index(), 0);
    // Check that the returned frames are correct
    const auto frames1 = std::get<1>(ret1);
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getFrameNumber(), framesList1[j] + 80);
        ++j;
    }
}

TEST_F(StreamTest1, getNextFrameSequence2)
{
    // Ensure that value in list is greater than buffer size
    const std::vector<int64_t> framesList1 = {3, 5, 7, 8, 12, 23};
    const auto ret1 = m_stream->getNextFrameSequence(framesList1);
    ASSERT_NE(ret1.index(), 0);
    // Check that the returned frames are correct
    const auto frames1 = std::get<1>(ret1);
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getFrameNumber(), framesList1[j]);
        ++j;
    }
}

// TODO: do all the same tests for each of the input data files