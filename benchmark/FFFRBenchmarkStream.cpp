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

constexpr uint32_t iterations = 15;

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
        m_stream = Stream::getStream(g_testData[0].m_fileName, options);
        if (m_stream == nullptr) {
            state.SkipWithError("Failed to create input stream");
            return;
        }
        m_timeJump = m_stream->frameToTime(state.range(0));
    }

    void TearDown(const ::benchmark::State&)
    {
        m_stream.reset();
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    int64_t m_timeJump = 0;
};

BENCHMARK_DEFINE_F(BenchStream, seekSeries)(benchmark::State& state)
{
    // Seek to end of iteration area as this ensures all additional loops within the benchmark start with the stream
    // in the same state
    if (!m_stream->seek(m_timeJump * iterations)) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    for (auto _ : state) {
        int64_t position = m_timeJump;
        for (int64_t i = 0; i < iterations; ++i) {
            if (!m_stream->seek(position)) {
                state.SkipWithError("Failed to seek");
                break;
            }
            if (m_stream->getNextFrame() == nullptr) {
                state.SkipWithError("Failed to retrieve valid frame");
                break;
            }
            position += m_timeJump;
        }
    }
}

BENCHMARK_DEFINE_F(BenchStream, seek)(benchmark::State& state)
{
    int64_t position = m_timeJump;
    for (auto _ : state) {
        if (!m_stream->seek(position)) {
            state.SkipWithError("Failed to seek");
            break;
        }
        if (m_stream->getNextFrame() == nullptr) {
            state.SkipWithError("Failed to retrieve valid frame");
            break;
        }
        position += m_timeJump;
    }
}

BENCHMARK_DEFINE_F(BenchStream, getNextFrameSeries)(benchmark::State& state)
{
    for (auto _ : state) {
        for (int64_t i = 0; i < iterations; ++i) {
            if (m_stream->getNextFrame() == nullptr) {
                state.SkipWithError("Failed to retrieve valid frame");
                break;
            }
        }
    }
}

BENCHMARK_DEFINE_F(BenchStream, getNextFrame)(benchmark::State& state)
{
    for (auto _ : state) {
        if (m_stream->getNextFrame() == nullptr) {
            state.SkipWithError("Failed to retrieve valid frame");
            break;
        }
    }
}

// Parameters in order are:
//  1. The number of frames to move forward in each seek
//  2. The buffer length
//  3. Boolean, 1 if cuda decoding should be used
static void customArguments(benchmark::internal::Benchmark* b)
{
    b->RangeMultiplier(2)->Ranges({{16, 256}, {1, 16}, {0, 1}})->Unit(benchmark::kMillisecond);
}

BENCHMARK_REGISTER_F(BenchStream, seekSeries)->Apply(customArguments);

BENCHMARK_REGISTER_F(BenchStream, seek)->Apply(customArguments);

BENCHMARK_REGISTER_F(BenchStream, getNextFrameSeries)->Apply(customArguments);

BENCHMARK_REGISTER_F(BenchStream, getNextFrame)->Apply(customArguments);