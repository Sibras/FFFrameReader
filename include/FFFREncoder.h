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
    friend class Fmc::MultiCrop;

public:
    FFFRAMEREADER_NO_EXPORT Encoder() = delete;

    FFFRAMEREADER_EXPORT ~Encoder() = default;

    FFFRAMEREADER_NO_EXPORT Encoder(const Encoder& other) = delete;

    FFFRAMEREADER_NO_EXPORT Encoder(Encoder&& other) noexcept = delete;

    FFFRAMEREADER_NO_EXPORT Encoder& operator=(const Encoder& other) = delete;

    FFFRAMEREADER_NO_EXPORT Encoder& operator=(Encoder&& other) noexcept = delete;

    /**
     * Encodes a stream to a file.
     * @param fileName File name of the file to write to.
     * @param stream   The stream to encode.
     * @param options  (Optional) Options for controlling encoding.
     * @returns The stream if succeeded, false otherwise.
     */
    FFFRAMEREADER_EXPORT static bool encodeStream(const std::string& fileName, const std::shared_ptr<Stream>& stream,
        const EncoderOptions& options = EncoderOptions()) noexcept;

    class ConstructorLock
    {
        friend class Encoder;
        friend class Fmc::MultiCrop;
    };

    /**
     * Constructor.
     * @param fileName    File name of the file to write to.
     * @param width       The output width.
     * @param height      The output height.
     * @param aspect      The sample aspect ratio.
     * @param format      Describes the format to use.
     * @param frameRate   The frame rate.
     * @param duration    The duration of the output encode.
     * @param codecType   Type of the codec to encode with.
     * @param quality     The encode quality.
     * @param preset      The encode preset.
     * @param numThreads  Number of threads to use.
     * @param gopSize     Size of the gop.
     */
    FFFRAMEREADER_NO_EXPORT Encoder(const std::string& fileName, uint32_t width, uint32_t height, Rational aspect,
        PixelFormat format, Rational frameRate, int64_t duration, EncodeType codecType, uint8_t quality,
        EncoderOptions::Preset preset, uint32_t numThreads, uint32_t gopSize, ConstructorLock) noexcept;

    /**
     * Query if this object is valid.
     * @returns True if the encoder is valid, false if not.
     */
    FFFRAMEREADER_EXPORT bool isEncoderValid() const noexcept;

private:
    OutputFormatContextPtr m_formatContext;
    CodecContextPtr m_codecContext;

    /**
     * Encodes all frames found in input stream from its current position.
     * @param stream The stream.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool encodeStream(const std::shared_ptr<Stream>& stream) const noexcept;

    /**
     * Encode frame.
     * @param frame The frame.
     * @param stream The stream.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool encodeFrame(
        const std::shared_ptr<Frame>& frame, const std::shared_ptr<Stream>& stream) const noexcept;

    /**
     * Writes encoded frames to output.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool muxFrames() const noexcept;
};
} // namespace Ffr
