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

#include <any>
#include <map>

namespace Ffr {
class DecoderContext;

class FfFrameReader
{
public:
    FfFrameReader();

    ~FfFrameReader() = default;

    FfFrameReader(const FfFrameReader& other) = default;

    FfFrameReader(FfFrameReader&& other) noexcept = default;

    FfFrameReader& operator=(const FfFrameReader& other) = default;

    FfFrameReader& operator=(FfFrameReader&& other) noexcept = default;

    enum class DecodeType
    {
        Software,
        CUDA,
    };

    class DecoderOptions
    {
    public:
        DecoderOptions(){};

        explicit DecoderOptions(DecodeType type) noexcept;

        ~DecoderOptions() = default;

        DecoderOptions(const DecoderOptions& other) = default;

        DecoderOptions(DecoderOptions&& other) = default;

        DecoderOptions& operator=(const DecoderOptions& other) = default;

        DecoderOptions& operator=(DecoderOptions&& other) = default;

        bool operator==(const DecoderOptions& other) const noexcept;

        bool operator!=(const DecoderOptions& other) const noexcept;

        bool operator<(const DecoderOptions& other) const noexcept;

        DecodeType m_type = DecodeType::Software; /**< The type of decoding to use. */
        uint32_t m_bufferLength = 10;             /**< Number of frames in the the decode buffer.
                                                  This should be optimised based on reading/seeking pattern so as to minimise frame
                                                  storage requirements but also maximise decode throughput. */
        std::any m_context;                       /**< Pointer to an existing context to be used for hardware
                                                   decoding. This must match the hardware type specified in @m_type. */
        uint32_t m_device = 0;                    /**< The device index for the desired hardware device. */
        bool m_outputHost = true; /**< True to output each frame to host CPU memory (only affects hardware decoding) */
    };

    /**
     * Gets a stream from a file.
     * @param filename Filename of the file to open.
     * @param options  (Optional) Options for controlling decoding.
     * @returns The stream if succeeded, false otherwise.
     */
    [[nodiscard]] std::variant<bool, std::shared_ptr<Stream>> getStream(
        const std::string& filename, const DecoderOptions& options = DecoderOptions()) const noexcept;

    /** Values that represent log levels */
    enum class LogLevel : int
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
    static void setLogLevel(LogLevel level);

    /**
     * Logs text to default output.
     * @param text  The text.
     * @param level (Optional) The logging level.
     */
    static void log(const std::string& text, LogLevel level = LogLevel::Info);

    /**
     * Static constructor
     * @param errorCode The error code.
     */
    static std::string getFfmpegErrorString(int errorCode);
};
} // namespace Ffr
