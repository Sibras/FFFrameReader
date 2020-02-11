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
#include "FFFRUtility.h"

#include "FFFrameReader.h"

extern "C" {
#include <libavutil/error.h>
}

using namespace std;

namespace Ffr {
std::string getFfmpegErrorString(const int errorCode) noexcept
{
    char buffer[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, errorCode);
    return buffer;
}

PixelFormat getPixelFormat(const AVPixelFormat format) noexcept
{
    switch (format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return PixelFormat::YUV420P;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            return PixelFormat::YUV422P;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            return PixelFormat::YUV444P;
        case AV_PIX_FMT_GBRP:
            return PixelFormat::RGB8P;
        case AV_PIX_FMT_RGB24:
            return PixelFormat::RGB8;
        case AV_PIX_FMT_NV12:
            return PixelFormat::NV12;
        case AV_PIX_FMT_GBRPF32LE:
            return PixelFormat::RGB32FP;
        default:
            try {
                logInternal(LogLevel::Error, "Unsupported pixel format detected: ", to_string(format));
            } catch (...) {
            }
            return PixelFormat::Auto;
    }
}

AVPixelFormat getPixelFormat(PixelFormat format) noexcept
{
    static_assert(static_cast<int>(PixelFormat::YUV420P) == AV_PIX_FMT_YUV420P, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::YUV422P) == AV_PIX_FMT_YUV422P, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::YUV444P) == AV_PIX_FMT_YUV444P, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::NV12) == AV_PIX_FMT_NV12, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::RGB8P) == AV_PIX_FMT_GBRP, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::RGB8) == AV_PIX_FMT_RGB24, "Pixel format mismatch detected");
    static_assert(static_cast<int>(PixelFormat::RGB32FP) == AV_PIX_FMT_GBRPF32LE, "Pixel format mismatch detected");
    // Can just do a direct cast
    return static_cast<AVPixelFormat>(format);
}

Rational getRational(const AVRational ratio) noexcept
{
    return Rational{ratio.num, ratio.den};
}

int64_t getPacketTimeStamp(const AVPacket& packet) noexcept
{
    return packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
}
} // namespace Ffr
