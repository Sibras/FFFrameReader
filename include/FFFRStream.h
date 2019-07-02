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
#include "FFFRTypes.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace Ffr {
class DecoderContext;
class Filter;
class Frame;

class Stream
{
    friend class Filter;
    friend class Encoder;
    friend class StreamUtils;

public:
    Stream() = delete;

    ~Stream() = default;

    Stream(const Stream& other) = delete;

    Stream(Stream&& other) noexcept = delete;

    Stream& operator=(const Stream& other) = delete;

    Stream& operator=(Stream&& other) noexcept = delete;

    /**
     * Gets a stream from a file.
     * @param fileName Filename of the file to open.
     * @param options  (Optional) Options for controlling decoding.
     * @returns The stream if succeeded, false otherwise.
     */
    [[nodiscard]] static std::shared_ptr<Stream> getStream(
        const std::string& fileName, const DecoderOptions& options = DecoderOptions()) noexcept;

    class ConstructorLock
    {
        friend class Stream;
    };

    /**
     * Constructor.
     * @param fileName       Filename of the file to open.
     * @param bufferLength   Number of frames in the the decode buffer.
     * @param decoderContext Pointer to an existing context to be used for hardware decoding.
     * @param outputHost     True to output each frame to host CPU memory (only affects hardware decoding).
     * @param crop           The output cropping or (0) if no crop should be performed.
     * @param scale          The output resolution or (0, 0) if no scaling should be performed. Scaling is performed
     *  after cropping.
     * @param format         The required output pixel format.
     */
    Stream(const std::string& fileName, uint32_t bufferLength, const std::shared_ptr<DecoderContext>& decoderContext,
        bool outputHost, Crop crop, Resolution scale, PixelFormat format, ConstructorLock) noexcept;

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
     * Gets the display aspect ratio of the video stream.
     * @note This may differ from width/height if stream uses anamorphic pixels.
     * @returns The aspect ratio.
     */
    [[nodiscard]] double getAspectRatio() const noexcept;

    /**
     * Gets the pixel format of the video stream.
     * @returns The pixel format.
     */
    [[nodiscard]] PixelFormat getPixelFormat() const noexcept;

    /**
     * Gets total frames in the video stream.
     * @returns The total frames.
     */
    [[nodiscard]] int64_t getTotalFrames() const noexcept;

    /**
     * Gets the duration of the video stream in micro-seconds.
     * @returns The duration.
     */
    [[nodiscard]] int64_t getDuration() const noexcept;

    /**
     * Gets the frame rate (fps) of the video stream.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @returns The frame rate in frames per second.
     */
    [[nodiscard]] double getFrameRate() const noexcept;

    /**
     * Gets the storage size of each decoded frame in the video stream.
     * @returns The frame size in bytes.
     */
    [[nodiscard]] uint32_t getFrameSize() const noexcept;

    /**
     * Get the next frame in the stream without removing it from stream buffer.
     * @returns The next frame in current stream, or nullptr if an error occured or end of file reached.
     */
    [[nodiscard]] std::shared_ptr<Frame> peekNextFrame() noexcept;

    /**
     * Gets the next frame in the stream and removes it from the buffer.
     * @returns The next frame in current stream, or nullptr if an error occured or end of file reached.
     */
    [[nodiscard]] std::shared_ptr<Frame> getNextFrame() noexcept;

    /**
     * Gets a sequence of frames offset from the current stream position using time stamps.
     * @param frameSequence The frame sequence. This is a list of offset times used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3000, 6000} will get the next frame as well as the the frame 3000us
     *  after this and then the frame 3000us after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Frame>> getNextFrames(const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames offset from the current stream position using frame indices.
     * @param frameSequence The frame sequence. This is a list of offset indices used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3, 6} will get the next frame as well as the 3rd frame after this and
     *  then the third frame after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Frame>> getNextFramesByIndex(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames based on there time stamps
     * @param frameSequence The frame sequence. This is a list of absolute times used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3000, 6000} will get the first frame as well as the the frame 3000us
     *  after this and then the frame 3000us after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Frame>> getFrames(const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames using frame indices.
     * @param frameSequence The frame sequence. This is a list of absolute indices used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3, 6} will get the first frame as well as the 3rd frame and then the
     *  third frame after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Frame>> getFramesByIndex(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Query if the stream has reached end of input file.
     * @returns True if end of file, false if not.
     */
    [[nodiscard]] bool isEndOfFile() const noexcept;

    /**
     * Seeks the stream to the given time stamp. If timestamp does not exactly match a frame then the timestamp rounded
     * down to nearest frame is used instead.
     * @param timeStamp The time stamp to seek to (in micro-seconds).
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool seek(int64_t timeStamp) noexcept;

    /**
     * Seeks the stream to the given frame number.
     * @param frame The zero-indexed frame number to seek to.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool seekFrame(int64_t frame) noexcept;

private:
    std::recursive_mutex m_mutex;

    uint32_t m_bufferLength = 0;                      /**< Length of the ping and pong buffers */
    std::vector<std::shared_ptr<Frame>> m_bufferPing; /**< The primary buffer used to store decoded frames */
    uint32_t m_bufferPingHead =
        0; /**< The position in the ping buffer of the next available frame in the decoded stream. */
    std::vector<std::shared_ptr<Frame>> m_bufferPong; /**< The secondary buffer used to store decoded frames */
    std::shared_ptr<Filter> m_filterGraph = nullptr;  /**< The filter graph for optional transformations. */
    bool m_outputHost = true; /**< True to output each frame to host CPU memory (only affects hardware decoding) */
    FramePtr m_tempFrame;     /**< The temporary frame used for decoding */

    FormatContextPtr m_formatContext;
    int32_t m_index = -1; /**< Zero-based index of the video stream  */
    CodecContextPtr m_codecContext;

    int64_t m_startTimeStamp = 0;        /**< PTS of the first frame in the stream time base */
    int64_t m_lastDecodedTimeStamp = -1; /**< The decoder time stamp of the last decoded frame */
    bool m_frameSeekSupported = true;    /**< True if frame seek supported */
    int64_t m_totalFrames = 0;           /**< Stream video duration in frames */
    int64_t m_totalDuration = 0;         /**< Stream video duration in microseconds (AV_TIME_BASE) */

    /**
     * Initialises codec parameters needed for future operations.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool initialise() noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to the stream timebase.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted time stamp.
     */
    [[nodiscard]] int64_t timeToTimeStamp(int64_t time) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to the codec timebase.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted time stamp.
     */
    [[nodiscard]] int64_t timeToTimeStamp2(int64_t time) const noexcept;

    /**
     * Convert a stream timebase to a time value represented in microseconds (AV_TIME_BASE).
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted time.
     */
    [[nodiscard]] int64_t timeStampToTime(int64_t timeStamp) const noexcept;

    /**
     * Convert a codec timebase to a time value represented in microseconds (AV_TIME_BASE).
     * @param timeStamp The time stamp represented in the codec internal time base.
     * @return The converted time.
     */
    [[nodiscard]] int64_t timeStampToTime2(int64_t timeStamp) const noexcept;

    /**
     * Convert a zero-based frame number to the stream timebase.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time stamp.
     */
    [[nodiscard]] int64_t frameToTimeStamp(int64_t frame) const noexcept;

    /**
     * Convert a zero-based frame number to the codec timebase.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time stamp.
     */
    [[nodiscard]] int64_t frameToTimeStamp2(int64_t frame) const noexcept;

    /**
     * Convert stream based time stamp to an equivalent zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted frame index.
     */
    [[nodiscard]] int64_t timeStampToFrame(int64_t timeStamp) const noexcept;

    /**
     * Convert codec based time stamp to an equivalent zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param timeStamp The time stamp represented in the codecs internal time base.
     * @return The converted frame index.
     */
    [[nodiscard]] int64_t timeStampToFrame2(int64_t timeStamp) const noexcept;

    /**
     * Convert a zero-based frame number to time value represented in microseconds (AV_TIME_BASE).
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time.
     */
    [[nodiscard]] int64_t frameToTime(int64_t frame) const noexcept;

    /**
     * Convert a zero-based codec frame number to time value represented in microseconds (AV_TIME_BASE).
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time.
     */
    [[nodiscard]] int64_t frameToTime2(int64_t frame) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to a zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted frame index.
     */
    [[nodiscard]] int64_t timeToFrame(int64_t time) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to a zero-based codec frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted frame index.
     */
    [[nodiscard]] int64_t timeToFrame2(int64_t time) const noexcept;

    /**
     * Decodes the next block of frames into the pong buffer. Once complete swaps the ping/pong buffers.
     * @param flushTillTime (Optional) All frames with decoder time stamps before this will be discarded.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool decodeNextBlock(int64_t flushTillTime = -1) noexcept;

    /**
     * Decodes any frames currently pending in the decoder.
     * @param flushTillTime (Optional) All frames with decoder time stamps before this will be discarded.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool decodeNextFrames(int64_t flushTillTime = -1) noexcept;

    /**
     * Pops the next available frame from the buffer.
     * @note This requires that peekNextFrame() be called first to ensure there is a valid frame to pop.
     */
    void popFrame() noexcept;

    /**
     * Return the maximum number of input frames needed by this stream's codec before it can produce output.
     * @note We expect to have to wait this many frames to receive output; any more and a decode stall is detected.
     * @returns The codec delay.
     */
    [[nodiscard]] int32_t getCodecDelay() const noexcept;

    /**
     * Gets the maximum possible frames that may occur between key frames.
     * @returns The codec key frame distance.
     */
    [[nodiscard]] int32_t getCodecKeyFrameDistance() const noexcept;

    /**
     * Return the maximum number of input frames needed by a codec before it can produce output.
     * @param codec The codec.
     * @returns The codec delay.
     */
    [[nodiscard]] static int32_t getCodecDelay(const CodecContextPtr& codec) noexcept;

    /**
     * Gets stream start time in the stream timebase.
     * @returns The stream start time.
     */
    [[nodiscard]] int64_t getStreamStartTime() const noexcept;

    /**
     * Gets total number of frames in a stream.
     * @returns The stream frames.
     */
    [[nodiscard]] int64_t getStreamFrames() const noexcept;

    /**
     * Gets the duration of a stream represented in microseconds (AV_TIME_BASE).
     * @returns The duration.
     */
    [[nodiscard]] int64_t getStreamDuration() const noexcept;
};
} // namespace Ffr