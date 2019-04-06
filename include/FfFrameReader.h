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
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace FfFrameReader {
#ifdef _DEBUG
#    define FFFRASSERT(cond, text, ret)     \
        {                                   \
            if (!(cond)) {                  \
                Interface::logError(text);  \
                Interface::logStackTrace(); \
                return ret;                 \
            }                               \
        }
#else
#    define FFFRASSERT(cond, text, ret)
#endif

class Frame;
class Stream;

class FrameInterface
{
    friend class StreamInterface;

public:
    FrameInterface() noexcept = default;

    explicit FrameInterface(std::shared_ptr<Frame> frame) noexcept;

    ~FrameInterface() noexcept = default;

    FrameInterface(const FrameInterface& other) noexcept = default;

    FrameInterface(FrameInterface&& other) noexcept = default;

    FrameInterface& operator=(const FrameInterface& other) noexcept = default;

    FrameInterface& operator=(FrameInterface&& other) noexcept = default;

    /**
     * Gets global time stamp for frame.
     * @returns The time stamp.
     */
    [[nodiscard]] int64_t getTimeStamp() const noexcept;

    /**
     * Gets picture sequence frame number.
     * @returns The frame number.
     */
    [[nodiscard]] int64_t getFrameNumber() const noexcept;

    /**
     * Gets frame data pointer.
     * @returns The internal frame data.
     */
    [[nodiscard]] uint8_t* getFrameData() const noexcept;

    /**
     * Gets the frame width.
     * @returns The width.
     */
    [[nodiscard]] uint32_t getWidth() const noexcept;

    /**
     * Gets the frame height.
     * @returns The height.
     */
    [[nodiscard]] uint32_t getHeight() const noexcept;

    /**
     * Gets the frame aspect ratio.
     * @returns The aspect ratio.
     */
    [[nodiscard]] double getAspectRatio() const noexcept;

private:
    std::shared_ptr<Frame> m_frame = nullptr;
};

class StreamInterface
{
    friend class Interface;

public:
    StreamInterface() noexcept = default;

    ~StreamInterface() noexcept = default;

    StreamInterface(const StreamInterface& other) noexcept = default;

    StreamInterface(StreamInterface&& other) noexcept = default;

    StreamInterface& operator=(const StreamInterface& other) noexcept = default;

    StreamInterface& operator=(StreamInterface&& other) noexcept = default;

    /**
     * Gets the width of the video stream.
     * @returns The width.
     */
    [[nodiscard]] uint32_t getWidth() const noexcept;

    /**
     * Gets the height of the video stream.
     * @returns The height.
     */
    [[nodiscard]] uint32_t getHeight() const noexcept;

    /**
     * Gets aspect ratio of the video stream.
     * @returns The aspect ratio.
     */
    [[nodiscard]] double getAspectRatio() const noexcept;

    /**
     * Gets total frames in the video stream.
     * @returns The total frames.
     */
    [[nodiscard]] int64_t getTotalFrames() const noexcept;

    /**
     * Gets the duration of the video stream in microseconds.
     * @returns The duration.
     */
    [[nodiscard]] int64_t getDuration() const noexcept;

    /**
     * Gets the next frame in the stream and removes it from the buffer.
     * @returns The next frame in current stream, or false if an error occured.
     */
    [[nodiscard]] std::variant<bool, FrameInterface> getNextFrame() const noexcept;

    /**
     * Gets a sequence of frames offset from the current stream position.
     * @param frameSequence The frame sequence. This is a monototincly increasing list of offset indices used to
     * specify which frames to retrieve. e.g. A sequence value of {0, 3, 6} will get the current next frame  as well as
     * the 3rd frame after this and then the third frame after that.
     * @returns A list of frames corresponding to the input sequence, or false if an error occured.
     */
    [[nodiscard]] std::variant<bool, std::vector<FrameInterface>> getNextFrameSequence(
        const std::vector<uint64_t>& frameSequence) const noexcept;

    /**
     * Seeks the stream to the given time stamp.
     * @param timeStamp The time stamp to seek to (in micro-seconds).
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool seek(int64_t timeStamp) const noexcept;

    /**
     * Seeks the stream to the given frame number.
     * @param frame The zero-indexed frame number to seek to.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool seekFrame(int64_t frame) const noexcept;

protected:
    explicit StreamInterface(std::shared_ptr<Stream> stream) noexcept;

private:
    std::shared_ptr<Stream> m_stream = nullptr;
};

class Interface
{
public:
    enum class DecoderType
    {
        Nvdec,
        Software,
    };

    /**
     * Opens a new stream from a file or retrieves an existing one if already open.
     * @note Once a stream has been finished with it should be released using releaseStream().
     * @param filename Filename of the file to open.
     * @param type     (Optional) The type of decoding to use. If a stream has already been created then this parameter
     *                 is ignored.
     * @returns The stream if succeeded, false otherwise.
     */
    static std::variant<bool, StreamInterface> getStream(
        const std::string& filename, DecoderType type = DecoderType::Software) noexcept;

    /**
     * Releases the stream described by filename
     * @param filename Filename of the file.
     */
    static void releaseStream(const std::string& filename) noexcept;

    enum class LogLevel
    {
        Info,
        Warning,
        Error
    };
    /**
     * Outputs desired text to the log file.
     * @param text     The text to log to file.
     * @param severity (Optional) The severity of the message.
     */
    static void logMessage(const std::string& text, LogLevel severity = LogLevel::Info) noexcept;

    /**
     * Outputs general information text to log file.
     * @param text The text.
     */
    static void logInfo(const std::string& text) noexcept;

    /**
     * Outputs warning text to log file.
     * @param text The text.
     */
    static void logWarning(const std::string& text) noexcept;

    /**
     * Outputs error text to log file.
     * @param text The text.
     */
    static void logError(const std::string& text) noexcept;

    /**
     * Outputs the current stack trace to file.
     * @note This is used for debugging purposes as it allows for printing the current
     *   stack trace after an error message is received.
     */
    static void logStackTrace() noexcept;
};
} // namespace FfFrameReader
