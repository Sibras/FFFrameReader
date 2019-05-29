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
} // namespace Ffr
