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

FfFrameReader::PixelFormat getPixelFormat(const AVPixelFormat format) noexcept
{
    switch (format) {
        case AV_PIX_FMT_YUV420P:
            return FfFrameReader::PixelFormat::YUV420P;
        case AV_PIX_FMT_YUV422P:
            return FfFrameReader::PixelFormat::YUV422P;
        case AV_PIX_FMT_YUV444P:
            return FfFrameReader::PixelFormat::YUV444P;
        case AV_PIX_FMT_GBRP:
            return FfFrameReader::PixelFormat::GBR8P;
        case AV_PIX_FMT_RGB24:
            return FfFrameReader::PixelFormat::RGB8;
        case AV_PIX_FMT_BGR24:
            return FfFrameReader::PixelFormat::BGR8;
        default:
            try {
                FfFrameReader::log(
                    "Unsupported pixel format detected: "s += to_string(format), FfFrameReader::LogLevel::Error);
            } catch (...) {
            }
            return FfFrameReader::PixelFormat::Auto;
    }
}
} // namespace Ffr