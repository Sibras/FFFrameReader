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
#pragma once
#include "FFFrameReader.h"

#include <string>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace Ffr {
/**
 * Gets ffmpeg error string.
 * @param errorCode The error code.
 * @returns The ffmpeg error string.
 */
FFFRAMEREADER_EXPORT std::string getFfmpegErrorString(int errorCode) noexcept;

/**
 * Gets pixel format for internal ffmpeg format
 * @param format Describes the format to use.
 * @returns The pixel format.
 */
FFFRAMEREADER_EXPORT PixelFormat getPixelFormat(AVPixelFormat format) noexcept;

/**
 * Gets ffmpeg pixel format.
 * @param format Describes the format to use.
 * @returns The pixel format.
 */
FFFRAMEREADER_EXPORT AVPixelFormat getPixelFormat(PixelFormat format) noexcept;

/**
 * Gets a rational.
 * @param ratio The ratio.
 * @returns The rational.
 */
FFFRAMEREADER_EXPORT Rational getRational(AVRational ratio);
} // namespace Ffr
