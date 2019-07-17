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

constexpr uint32_t iterations = 4;

class BenchStream : public benchmark::Fixture
{
public:
    void SetUp(::benchmark::State& state)
    {
        setLogLevel(LogLevel::Quiet);
        DecoderOptions options;
        options.m_bufferLength = static_cast<uint32_t>(state.range(1));
        if (state.range(2) == 1) {
            options.m_type = DecodeType::Cuda;
            options.m_outputHost = false;
        }
        options.m_noBufferFlush = state.range(3) == 1;
        m_stream = Stream::getStream(g_testData[0].m_fileName, options);
        if (m_stream == nullptr) {
            state.SkipWithError("Failed to create input stream");
        }
    }

    void TearDown(const ::benchmark::State&)
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
};

BENCHMARK_DEFINE_F(BenchStream, seekSeries)(benchmark::State& state)
{
    if (state.range(0) * iterations >= m_stream->getTotalFrames()) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    for (auto _ : state) {
        state.PauseTiming();
        // Ignore the seek back to start after each loop
        (void)m_stream->seek(0);
        state.ResumeTiming();
        for (int64_t i = 0; i < iterations; ++i) {
            if (!m_stream->seek(m_stream->frameToTime(state.range(0) * i))) {
                state.SkipWithError("Failed to seek");
                break;
            }
            if (m_stream->getNextFrame() == nullptr) {
                state.SkipWithError("Failed to retrieve valid frame");
                break;
            }
        }
    }
}

BENCHMARK_DEFINE_F(BenchStream, seek)(benchmark::State& state)
{
    if (state.range(0) * iterations >= m_stream->getTotalFrames()) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    int64_t position = 1;
    for (auto _ : state) {
        if (!m_stream->seek(m_stream->frameToTime(state.range(0) * position))) {
            state.SkipWithError("Failed to seek");
            break;
        }
        if (m_stream->getNextFrame() == nullptr) {
            state.SkipWithError("Failed to retrieve valid frame");
            break;
        }
        ++position;
    }
}

// Parameters in order are:
//  1. The number of frames to move forward in each seek
//  2. The buffer length
//  3. Boolean, 1 if cuda decoding should be used
//  4. Boolean, 1 if no buffer flush should be set
static void customArguments(benchmark::internal::Benchmark* b)
{
    b->RangeMultiplier(2)->Ranges({{2, 256}, {1, 16}, {0, 1}, {0, 1}})->Unit(benchmark::kMillisecond);
}

BENCHMARK_REGISTER_F(BenchStream, seekSeries)->Apply(customArguments);

BENCHMARK_REGISTER_F(BenchStream, seek)->Apply(customArguments);