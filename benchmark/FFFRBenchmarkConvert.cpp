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
#include <cuda.h>

using namespace Ffr;

constexpr int32_t iterations = 15;

class BenchConvert : public benchmark::Fixture
{
public:
    void SetUp(::benchmark::State& state)
    {
        setLogLevel(LogLevel::Quiet);

        // Create a cuda context to be shared by decoder and this test
        cuInit(0);
        CUcontext context = nullptr;
        cuCtxGetCurrent(&context);
        if (m_context == nullptr) {
            CUdevice dev;
            cuDeviceGet(&dev, 0);
            cuDevicePrimaryCtxRetain(&context, dev);
            m_context = std::shared_ptr<std::remove_pointer<CUcontext>::type>(
                context, [dev](CUcontext) { cuDevicePrimaryCtxRelease(dev); });
        } else {
            m_context = std::shared_ptr<std::remove_pointer<CUcontext>::type>(context, [](CUcontext) {});
        }

        DecoderOptions options;
        options.m_bufferLength = static_cast<uint32_t>(state.range(1));
        options.m_type = DecodeType::Cuda;
        options.m_outputHost = false;
        options.m_context = m_context.get();
        m_stream = Stream::getStream(g_testData[0].m_fileName, options);
        if (m_stream == nullptr) {
            state.SkipWithError("Failed to create input stream");
            return;
        }
        m_timeJump = m_stream->frameToTime(state.range(0));

        // Get frame dimensions
        const int width = m_stream->peekNextFrame()->getWidth();
        const int height = m_stream->peekNextFrame()->getHeight();

        // Allocate new memory to store frame data
        cuCtxPushCurrent(m_context.get());
        const auto outFrameSize = getImageSize(PixelFormat::RGB32FP, width, height);
        cuMemAlloc(&m_cudaBuffer, outFrameSize);
        CUcontext dummy;
        cuCtxPopCurrent(&dummy);

        m_blockSize = std::min(static_cast<int32_t>(state.range(2)), iterations);
    }

    void TearDown(const ::benchmark::State&)
    {
        m_stream.reset();
        if (reinterpret_cast<void*>(m_cudaBuffer) != nullptr) {
            cuCtxPushCurrent(m_context.get());
            cuMemFree(m_cudaBuffer);
            CUcontext dummy;
            cuCtxPopCurrent(&dummy);
        }
        m_context = nullptr;
    }

    std::shared_ptr<Stream> m_stream = nullptr;
    int64_t m_timeJump = 0;
    int32_t m_blockSize = 0;
    std::shared_ptr<std::remove_pointer<CUcontext>::type> m_context = nullptr;
    CUdeviceptr m_cudaBuffer = reinterpret_cast<CUdeviceptr>(nullptr);
};

BENCHMARK_DEFINE_F(BenchConvert, seekConvertSeries)(benchmark::State& state)
{
    // Seek to end of iteration area as this ensures all additional loops within the benchmark start with the stream
    // in the same state
    if (!m_stream->seek(m_timeJump * iterations)) {
        state.SkipWithError("Cannot perform required iterations on input stream");
    }
    // Generate list of seeks
    std::vector<int64_t> frames;
    for (int64_t i = 0; i < iterations; ++i) {
        frames.emplace_back(i * m_timeJump);
    }
    for (auto _ : state) {
        const auto blocks = std::div(static_cast<int32_t>(frames.size()), m_blockSize);
        const auto numBlocks = blocks.quot + (blocks.rem > 0 ? 1 : 0);
        uint32_t count = 0;
        std::shared_ptr<Frame> lastFrame = nullptr;
        for (int32_t i = 0; i < numBlocks; ++i) {
            const auto start = frames.begin() + (i * m_blockSize);
            const auto last = (i + 1) * m_blockSize;
            const auto end = frames.begin() +
                (last <= static_cast<int32_t>(frames.size()) ? last : static_cast<int32_t>(frames.size()));
            const std::vector<int64_t> frameSequence(start, end);
            const auto retrieved = m_stream->getFrames(frameSequence);
            for (auto& j : retrieved) {
                if (!convertFormatAsync(j, reinterpret_cast<uint8_t*>(m_cudaBuffer), PixelFormat::RGB32FP)) {
                    break;
                }
                ++count;
            }
            if (i == numBlocks - 1) {
                lastFrame = retrieved.back();
            }
        }
        if (count != frames.size()) {
            if (!m_stream->isEndOfFile()) {
                state.SkipWithError("Cannot perform required iterations on input stream");
            }
        }
        if (!synchroniseConvert(lastFrame)) {
            state.SkipWithError("CUDA convert failed");
        }
    }
}

// Parameters in order are:
//  1. The number of frames to move forward in each seek
//  2. The buffer length
//  3. The batch block size
static void customArguments(benchmark::internal::Benchmark* b)
{
    b->RangeMultiplier(2)->Ranges({{1, 256}, {1, 16}, {1, 8}})->Unit(benchmark::kMillisecond);
}

BENCHMARK_REGISTER_F(BenchConvert, seekConvertSeries)->Apply(customArguments);