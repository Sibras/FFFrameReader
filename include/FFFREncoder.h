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
class Encoder
{
public:
    Encoder() = delete;

    ~Encoder() = default;

    Encoder(const Encoder& other) = delete;

    Encoder(Encoder&& other) noexcept = delete;

    Encoder& operator=(const Encoder& other) = delete;

    Encoder& operator=(Encoder&& other) noexcept = delete;

    /**
     * Gets a stream from a file.
     * @param fileName File name of the file to write to.
     * @param stream   The stream to encode.
     * @param options  (Optional) Options for controlling encoding.
     * @returns The stream if succeeded, false otherwise.
     */
    [[nodiscard]] static bool encodeStream(const std::string& fileName, const std::shared_ptr<Stream>& stream,
        const EncoderOptions& options = EncoderOptions()) noexcept;

private:
    class OutputFormatContextPtr
    {
        friend class Encoder;

        OutputFormatContextPtr() = default;

        explicit OutputFormatContextPtr(AVFormatContext* formatContext) noexcept;

        [[nodiscard]] AVFormatContext* get() const noexcept;

        AVFormatContext* operator->() const noexcept;

        std::shared_ptr<AVFormatContext> m_formatContext = nullptr;
    };

    using CodecContextPtr = Stream::CodecContextPtr;

    OutputFormatContextPtr m_formatContext;
    CodecContextPtr m_codecContext;
    std::shared_ptr<Stream> m_stream;

    /**
     * Constructor.
     * @param fileName  File name of the file to write to.
     * @param stream    The stream to use as input.
     * @param codecType Type of the codec to encode with.
     * @param quality   The encode quality.
     * @param preset    The encode preset.
     */
    Encoder(const std::string& fileName, const std::shared_ptr<Stream>& stream, EncodeType codecType, uint8_t quality,
        EncoderOptions::Preset preset) noexcept;

    /**
     * Encodes all frames found in input stream from its current position.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool encodeStream() const noexcept;

    /**
     * Writes encoded frames to output.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool encodeFrames() const noexcept;
};
} // namespace Ffr