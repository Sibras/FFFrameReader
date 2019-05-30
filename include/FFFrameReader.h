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
#pragma once
#include "FFFRStream.h"

namespace Ffr {
/** Values that represent log levels */
enum class LogLevel : int32_t
{
    Quiet = -8,
    Panic = 0,
    Fatal = 8,
    Error = 16,
    Warning = 24,
    Info = 32,
    Verbose = 40,
    Debug = 48,
};

/**
 * Sets log level for all functions within FfFrameReader.
 * @param level The level.
 */
void setLogLevel(LogLevel level);

/**
 * Logs text to default output.
 * @param text  The text.
 * @param level (Optional) The logging level.
 */
void log(const std::string& text, LogLevel level = LogLevel::Info);

/**
 * Gets number of planes for an image of the specified pixel format
 * @param format Describes the pixel format.
 * @returns The number of planes (YUV420P has 3, RGB8 has 1 etc.) or negative value if invalid format.
 */
int32_t getPixelFormatPlanes(PixelFormat format);

/**
 * Gets image size for frame with specified properties
 * @param format Describes the pixel format.
 * @param width  The width.
 * @param height The height.
 * @returns The image size or negative value if error.
 */
int32_t getImageSize(PixelFormat format, uint32_t width, uint32_t height);

/**
 * Convert pixel format using cuda.
 * @param       frame     The input frame.
 * @param [out] outMem    Memory location to store output (must be allocated with enough size for outpur frame see
 *  @getImageSize).
 * @param       outFormat The pixel format to convert to.
 * @returns True if it succeeds, false if it fails.
 */
bool convertFormat(const std::shared_ptr<Frame>& frame, uint8_t* outMem[3], PixelFormat outFormat) noexcept;
} // namespace Ffr