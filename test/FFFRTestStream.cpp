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

#include <gtest/gtest.h>

using namespace FfFrameReader;

class StreamTest1 : public ::testing::TestWithParam<TestParams>
{
protected:
    StreamTest1() = default;

    void SetUp() override
    {
        auto ret = m_manager.getStream(GetParam().m_fileName);
        ASSERT_NE(ret.index(), 0);
        m_stream = std::get<1>(ret);
    }

    ~StreamTest1() override
    {
        m_manager.releaseStream(GetParam().m_fileName);
    }

    Manager m_manager;
    std::shared_ptr<Stream> m_stream;
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

TEST_P(StreamTest1, getFrameTime)
{
    ASSERT_EQ(m_stream->getFrameTime(), GetParam().m_frameTime);
}

// TODO: Add stream->getFrameTime

TEST_P(StreamTest1, getNextFrame)
{
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
}

TEST_P(StreamTest1, getNextFrame2)
{
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto ret2 = m_stream->getNextFrame();
    ASSERT_NE(ret2.index(), 0);
    // TODO: Check that the frames are in fact different
}

TEST_P(StreamTest1, getNextFrameLoop)
{
    // Ensure that multiple frames can be read
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
    }
}

TEST_P(StreamTest1, getLoop)
{
    // Ensure that multiple frames can be read
    int64_t timeStamp = 0;
    int64_t frameNum = 0;
    for (uint32_t i = 0; i < 25; i++) {
        const auto ret1 = m_stream->getNextFrame();
        ASSERT_NE(ret1.index(), 0);
        const auto frame1 = std::get<1>(ret1);
        ASSERT_EQ(frame1->getTimeStamp(), timeStamp);
        timeStamp += GetParam().m_frameTime;
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}

TEST_P(StreamTest1, getLoopAll)
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
        timeStamp += GetParam().m_frameTime;
        ASSERT_EQ(frame1->getFrameNumber(), frameNum);
        ++frameNum;
    }
}

TEST_P(StreamTest1, seek)
{
    const int64_t time1 = GetParam().m_frameTime * 80;
    ASSERT_TRUE(m_stream->seek(time1));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekSmall)
{
    // First fill the buffer
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
    // Seek forward 2 frames only. This should just increment the existing buffer
    const int64_t time1 = GetParam().m_frameTime * 2;
    ASSERT_TRUE(m_stream->seek(time1));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekMedium)
{
    // First fill the buffer
    ASSERT_NE(m_stream->getNextFrame().index(), 0);
    // Seek forward 1.5 * bufferSize frame TODO**********
    const int64_t time1 = GetParam().m_frameTime * 15;
    ASSERT_TRUE(m_stream->seek(time1));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekFail)
{
    ASSERT_FALSE(m_stream->seek(m_stream->getDuration()));
}

TEST_P(StreamTest1, seekEnd)
{
    ASSERT_TRUE(m_stream->seek(m_stream->getDuration() - GetParam().m_frameTime));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
}

TEST_P(StreamTest1, seekLoop)
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
            timeStamp2 += GetParam().m_frameTime;
        }
        timeStamp += GetParam().m_frameTime * 40;
    }
}

TEST_P(StreamTest1, seekBack)
{
    // Seek forward
    const int64_t time1 = GetParam().m_frameTime * 80;
    ASSERT_TRUE(m_stream->seek(time1));
    auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
    // Seek back
    ASSERT_TRUE(m_stream->seek(0));
    ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getTimeStamp(), 0);
}

TEST_P(StreamTest1, seekFrame)
{
    const int64_t frameNum1 = 80;
    ASSERT_TRUE(m_stream->seekFrame(frameNum1));
    const auto ret1 = m_stream->getNextFrame();
    ASSERT_NE(ret1.index(), 0);
    const auto frame1 = std::get<1>(ret1);
    ASSERT_EQ(frame1->getFrameNumber(), frameNum1);
}

TEST_P(StreamTest1, seekFrameLoop)
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
        frame += 40;
    }
}

TEST_P(StreamTest1, getNextFrameSequence)
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

TEST_P(StreamTest1, getNextFrameSequenceSeek)
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

TEST_P(StreamTest1, getNextFrameSequence2)
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

INSTANTIATE_TEST_SUITE_P(StreamTestData, StreamTest1, ::testing::ValuesIn(g_testData));