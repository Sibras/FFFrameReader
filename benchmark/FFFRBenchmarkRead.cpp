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
#include "../test/FFFRTestData.h"
#include "FFFrameReader.h"

#include <benchmark/benchmark.h>

using namespace Ffr;

constexpr uint32_t iterations = 50;

class BenchRead : public benchmark::Fixture
{
public:
    void SetUp(::benchmark::State& state)
    {
        setLogLevel(LogLevel::Quiet);
        DecoderOptions options;
        options.m_bufferLength = static_cast<uint32_t>(state.range(0));
        if (state.range(1) == 1) {
            options.m_type = DecodeType::Cuda;
            options.m_outputHost = false;
        }
        m_stream = Stream::getStream(g_testData[0].m_fileName, options);
        if (m_stream == nullptr) {
            state.SkipWithError("Failed to create input stream");
            return;
        }
    }

    void TearDown(const ::benchmark::State&)
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
};

BENCHMARK_DEFINE_F(BenchRead, read)(benchmark::State& state)
{
    if (iterations >= m_stream->getTotalFrames()) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    for (auto _ : state) {
        for (int64_t i = 0; i < iterations; ++i) {
            if (m_stream->getNextFrame() == nullptr) {
                state.SkipWithError("Failed to retrieve valid frame");
                break;
            }
        }
    }
}

BENCHMARK_DEFINE_F(BenchRead, readBatch)(benchmark::State& state)
{
    if (iterations >= m_stream->getTotalFrames()) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    constexpr int32_t blockSize = 5;
    std::vector<int64_t> frames;
    for (int64_t i = 0; i < iterations; i++) {
        frames.emplace_back(m_stream->frameToTime(i));
    }
    for (auto _ : state) {
        state.PauseTiming();
        // Ignore the seek back to start after each loop
        (void)m_stream->seek(0);
        state.ResumeTiming();
        const auto blocks = std::div(static_cast<int32_t>(frames.size()), blockSize);
        const auto numBlocks = blocks.quot + (blocks.rem > 0 ? 1 : 0);
        for (int32_t i = 0; i < numBlocks; ++i) {
            const auto start = &frames.data()[i * blockSize];
            const auto last = (i + 1) * blockSize;
            const auto end = &frames.data()[std::min(static_cast<int32_t>(frames.size()), last)];
            const std::vector<int64_t> frameSequence(start, end);
            if (m_stream->getFrames(frameSequence).size() != frameSequence.size()) {
                state.SkipWithError("Failed to retrieve valid frame");
                break;
            }
        }
    }
}

// Parameters in order are:
//  1. The buffer length
//  2. Boolean, 1 if cuda decoding should be used
static void customArguments(benchmark::internal::Benchmark* b)
{
    b->RangeMultiplier(2)->Ranges({{1, 16}, {0, 1}})->Unit(benchmark::kMillisecond);
}

// BENCHMARK_REGISTER_F(BenchRead, read)->Apply(customArguments);

// BENCHMARK_REGISTER_F(BenchRead, readBatch)->Apply(customArguments);