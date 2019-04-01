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
#include <fstream>
#include <mutex>
#include <string>

namespace FfFrameReader {
class Log final
{
public:
    enum class LogLevel
    {
        Info,
        Warning,
        Error
    };

    using LogLevel = Log::LogLevel;

    /** Default constructor. */
    Log() = default;

    /** Destructor. */
    ~Log() noexcept;

    Log(const Log&) noexcept = delete;

    Log(Log&&) noexcept = delete;

    Log& operator=(const Log&) noexcept = delete;

    Log& operator=(Log&&) noexcept = delete;

    /**
     * Outputs desired text to the log file.
     * @param text     The text to log to file.
     * @param severity (Optional) The severity of the message.
     */
    void logMessage(const std::string& text, LogLevel severity = LogLevel::Info);

    /**
     * Outputs general information text to log file.
     * @param text The text.
     */
    void logInfo(const std::string& text);

    /**
     * Outputs warning text to log file.
     * @param text The text.
     */
    void logWarning(const std::string& text);

    /**
     * Outputs error text to log file.
     * @param text The text.
     */
    void logError(const std::string& text);

    /**
     * Outputs the current stack trace to file.
     * @note This is used for debugging purposes as it allows for printing the current
     *   stack trace after an error message is received.
     */
    void logStackTrace();

    /**
     * Converts an FFmpeg error code to a readable string.
     * @param err The error code.
     * @returns The error string.
     */
    static std::string getFfmpegErrorString(int err);

private:
    /**
     * Loads the log.
     * @returns True if it succeeds, false if it fails.
     */
    bool load() noexcept;

    std::fstream m_fileHandle;
    std::mutex m_mutex;
};
} // namespace FfFrameReader
