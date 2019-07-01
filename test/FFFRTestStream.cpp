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

TEST_P(StreamTest1, seek)
{
    constexpr int64_t seekFrames = 80;
    const int64_t actualSeek = seekFrames >= GetParam().m_totalFrames ? GetParam().m_totalFrames - 5 : seekFrames;
    const double timeStamp1 = (static_cast<double>(actualSeek) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    const auto frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekSmall)
{
    ASSERT_NE(m_stream->getNextFrame(), nullptr);
    // Seek forward 2 frames only.
    const double timeStamp1 = (static_cast<double>(2) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    const auto frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekFail)
{
    ASSERT_FALSE(m_stream->seek(m_stream->getDuration()));
    ASSERT_FALSE(m_stream->seek(m_stream->getDuration() + 300000));
    // Check we can do a valid seek after a failed one
    const double timeStamp1 = (static_cast<double>(2) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    const auto frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
}

TEST_P(StreamTest1, seekEnd)
{
    ASSERT_TRUE(m_stream->seek(
        m_stream->getDuration() - GetParam().m_frameTime - ((((GetParam().m_frameTime / 3) & 0x3) == 2) ? 1 : 0)));
    const auto frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
}

TEST_P(StreamTest1, seekLoop)
{
    constexpr uint32_t seekJump = 40;
    constexpr uint32_t numLoops = 5;
    constexpr uint32_t numFrames = 25;
    if (GetParam().m_totalFrames >= numLoops * (seekJump + numFrames)) {
        double timeStamp1 = 0.0;
        int64_t time1 = 0;
        // Perform multiple forward seeks
        for (uint32_t i = 0; i < numLoops; i++) {
            ASSERT_TRUE(m_stream->seek(time1));
            // Check that multiple sequential frames can be read
            int64_t time2 = time1;
            for (uint32_t j = 0; j < numFrames; j++) {
                const auto frame1 = m_stream->getNextFrame();
                // Allow EOF
                if (frame1 == nullptr && m_stream->isEndOfFile()) {
                    return;
                }
                ASSERT_NE(frame1, nullptr);
                ASSERT_EQ(frame1->getTimeStamp(), time2);
                const double timeStamp2 =
                    timeStamp1 + (static_cast<double>(j + 1) * (1000000.0 / GetParam().m_frameRate));
                time2 = llround(timeStamp2);
            }
            timeStamp1 =
                (static_cast<double>(i + 1) * static_cast<double>(seekJump) * (1000000.0 / GetParam().m_frameRate));
            time1 = llround(timeStamp1);
        }
    }
}

TEST_P(StreamTest1, seekBack)
{
    // Seek forward
    constexpr int64_t seekFrames = 80;
    const int64_t actualSeek = seekFrames >= GetParam().m_totalFrames ? GetParam().m_totalFrames - 5 : seekFrames;
    const double timeStamp1 = (static_cast<double>(actualSeek) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    auto frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), time1);
    // Seek back
    ASSERT_TRUE(m_stream->seek(0));
    frame1 = m_stream->getNextFrame();
    ASSERT_NE(frame1, nullptr);
    ASSERT_EQ(frame1->getTimeStamp(), 0);
}

TEST_P(StreamTest1, seekFrameLoop)
{
    constexpr uint32_t seekJump = 40;
    constexpr uint32_t numLoops = 5;
    constexpr uint32_t numFrames = 25;
    if (GetParam().m_totalFrames >= numLoops * (seekJump + numFrames)) {
        int64_t frame = 0;
        // Perform multiple forward seeks
        for (uint32_t i = 0; i < numLoops; i++) {
            ASSERT_TRUE(m_stream->seekFrame(frame));
            // Check that multiple sequential frames can be read
            int64_t frame2 = frame;
            for (uint32_t j = 0; j < numFrames; j++) {
                const auto frame1 = m_stream->getNextFrame();
                // Allow EOF
                if (frame1 == nullptr && m_stream->isEndOfFile()) {
                    return;
                }
                ASSERT_NE(frame1, nullptr);
                ASSERT_EQ(frame1->getFrameNumber(), frame2);
                ++frame2;
            }
            frame += seekJump;
        }
    }
}

TEST_P(StreamTest1, getNextFrameSequenceSeek)
{
    // First seek to a frame
    constexpr int64_t seekFrames = 80;
    const int64_t actualSeek = seekFrames >= GetParam().m_totalFrames ? GetParam().m_totalFrames - 9 : seekFrames;
    ASSERT_TRUE(m_stream->seekFrame(actualSeek));
    // Now get frame sequence offset from current
    const std::vector<int64_t> framesList1 = {0, 1, 5, 7, 8};
    const auto frames1 = m_stream->getNextFrameSequence(framesList1);
    ASSERT_EQ(frames1.size(), framesList1.size());
    // Check that the returned frames are correct
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getFrameNumber(), framesList1[j] + actualSeek);
        ++j;
    }
}

TEST_P(StreamTest1, getNextFrameSequence)
{
    // Ensure that value in list is greater than buffer size
    const std::vector<int64_t> framesList1 = {3, 5, 7, 8, 12, 23};
    const auto frames1 = m_stream->getNextFrameSequence(framesList1);
    ASSERT_EQ(frames1.size(), framesList1.size());
    // Check that the returned frames are correct
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getFrameNumber(), framesList1[j]);
        ++j;
    }
}

TEST_P(StreamTest1, getNextFramesSeek)
{
    // First seek to a frame
    constexpr int64_t seekFrames = 80;
    const int64_t actualSeek = seekFrames >= GetParam().m_totalFrames ? GetParam().m_totalFrames - 9 : seekFrames;
    const double timeStamp1 = (static_cast<double>(actualSeek) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    // Now get frame sequence offset from current
    const std::vector<int64_t> framesList1 = {0, 1, 5, 7, 8};
    std::vector<int64_t> timesList1;
    for (const auto& i : framesList1) {
        const double timeStamp2 = (static_cast<double>(i) * (1000000.0 / GetParam().m_frameRate));
        const auto time2 = llround(timeStamp2);
        timesList1.emplace_back(time2);
    }
    const auto frames1 = m_stream->getNextFrames(timesList1);
    ASSERT_EQ(frames1.size(), timesList1.size());
    // Check that the returned frames are correct
    auto j = 0;
    for (auto& i : frames1) {
        const double timeStamp2 =
            static_cast<double>(framesList1[j] + actualSeek) * (1000000.0 / GetParam().m_frameRate);
        const auto time2 = llround(timeStamp2);
        ASSERT_EQ(i->getTimeStamp(), time2);
        ++j;
    }
}

TEST_P(StreamTest1, getNextFrames)
{
    // Ensure that value in list is greater than buffer size
    const std::vector<int64_t> framesList1 = {3, 5, 7, 8, 12, 23};
    std::vector<int64_t> timesList1;
    for (const auto& i : framesList1) {
        const double timeStamp2 = (static_cast<double>(i) * (1000000.0 / GetParam().m_frameRate));
        const auto time2 = llround(timeStamp2);
        timesList1.emplace_back(time2);
    }
    const auto frames1 = m_stream->getNextFrames(timesList1);
    ASSERT_EQ(frames1.size(), timesList1.size());
    // Check that the returned frames are correct
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getTimeStamp(), timesList1[j]);
        ++j;
    }
}

TEST_P(StreamTest1, getFrames)
{
    // Seek forward 2 frames only. This should not effect result
    const double timeStamp1 = (static_cast<double>(2) * (1000000.0 / GetParam().m_frameRate));
    const auto time1 = llround(timeStamp1);
    ASSERT_TRUE(m_stream->seek(time1));
    // Ensure that value in list is greater than buffer size
    const std::vector<int64_t> framesList1 = {3, 5, 7, 8, 12, 23};
    std::vector<int64_t> timesList1;
    for (const auto& i : framesList1) {
        const double timeStamp2 = (static_cast<double>(i) * (1000000.0 / GetParam().m_frameRate));
        const auto time2 = llround(timeStamp2);
        timesList1.emplace_back(time2);
    }
    const auto frames1 = m_stream->getFrames(timesList1);
    ASSERT_EQ(frames1.size(), timesList1.size());
    // Check that the returned frames are correct
    auto j = 0;
    for (auto& i : frames1) {
        ASSERT_EQ(i->getTimeStamp(), timesList1[j]);
        ++j;
    }
}

INSTANTIATE_TEST_SUITE_P(StreamTestData, StreamTest1, ::testing::ValuesIn(g_testData));