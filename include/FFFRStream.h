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
    friend class FFR;

public:
    FFFRAMEREADER_NO_EXPORT Stream() = delete;

    FFFRAMEREADER_EXPORT ~Stream() = default;

    FFFRAMEREADER_NO_EXPORT Stream(const Stream& other) = delete;

    FFFRAMEREADER_NO_EXPORT Stream(Stream&& other) noexcept = delete;

    FFFRAMEREADER_NO_EXPORT Stream& operator=(const Stream& other) = delete;

    FFFRAMEREADER_NO_EXPORT Stream& operator=(Stream&& other) noexcept = delete;

    /**
     * Gets a stream from a file.
     * @param fileName Filename of the file to open.
     * @param options  (Optional) Options for controlling decoding.
     * @returns The stream if succeeded, nullptr otherwise.
     */
    FFFRAMEREADER_EXPORT static std::shared_ptr<Stream> getStream(
        const std::string& fileName, const DecoderOptions& options = DecoderOptions()) noexcept;

    class ConstructorLock
    {
        friend class Stream;
    };

    /**
     * Constructor.
     * @param fileName       Filename of the file to open.
     * @param bufferLength   Number of frames in the the decode buffer.
     * @param seekThreshold  Maximum number of frames for a forward seek to continue to decode instead of seeking.
     * @param noBufferFlush  True to skip buffer flushing on seeks.
     * @param decoderContext Pointer to an existing context to be used for hardware decoding.
     * @param outputHost     True to output each frame to host CPU memory (only affects hardware decoding).
     * @param crop           The output cropping or (0) if no crop should be performed.
     * @param scale          The output resolution or (0, 0) if no scaling should be performed. Scaling is performed
     *  after cropping.
     * @param format         The required output pixel format.
     */
    FFFRAMEREADER_NO_EXPORT Stream(const std::string& fileName, uint32_t bufferLength, uint32_t seekThreshold,
        bool noBufferFlush, const std::shared_ptr<DecoderContext>& decoderContext, bool outputHost, Crop crop,
        Resolution scale, PixelFormat format, ConstructorLock) noexcept;

    /**
     * Gets the width of the video stream.
     * @returns The width.
     */
    FFFRAMEREADER_EXPORT uint32_t getWidth() const noexcept;

    /**
     * Gets the height of the video stream.
     * @returns The height.
     */
    FFFRAMEREADER_EXPORT uint32_t getHeight() const noexcept;

    /**
     * Gets the display aspect ratio of the video stream.
     * @note This may differ from width/height if stream uses anamorphic pixels.
     * @returns The aspect ratio.
     */
    FFFRAMEREADER_EXPORT double getAspectRatio() const noexcept;

    /**
     * Gets the pixel format of the video stream.
     * @returns The pixel format.
     */
    FFFRAMEREADER_EXPORT PixelFormat getPixelFormat() const noexcept;

    /**
     * Gets total frames in the video stream.
     * @returns The total frames.
     */
    FFFRAMEREADER_EXPORT int64_t getTotalFrames() const noexcept;

    /**
     * Gets the duration of the video stream in micro-seconds.
     * @returns The duration.
     */
    FFFRAMEREADER_EXPORT int64_t getDuration() const noexcept;

    /**
     * Gets the frame rate (fps) of the video stream.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @returns The frame rate in frames per second.
     */
    FFFRAMEREADER_EXPORT double getFrameRate() const noexcept;

    /**
     * Gets the storage size of each decoded frame in the video stream.
     * @returns The frame size in bytes.
     */
    FFFRAMEREADER_EXPORT uint32_t getFrameSize() const noexcept;

    /**
     * Gets the type of decoding used.
     * @returns The decode type.
     */
    FFFRAMEREADER_EXPORT DecodeType getDecodeType() const noexcept;

    /**
     * Get the next frame in the stream without removing it from stream buffer.
     * @returns The next frame in current stream, or nullptr if an error occured or end of file reached.
     */
    FFFRAMEREADER_EXPORT std::shared_ptr<Frame> peekNextFrame() noexcept;

    /**
     * Gets the next frame in the stream and removes it from the buffer.
     * @returns The next frame in current stream, or nullptr if an error occured or end of file reached.
     */
    FFFRAMEREADER_EXPORT std::shared_ptr<Frame> getNextFrame() noexcept;

    /**
     * Gets maximum frames that can exist at a time.
     * @remark This is effected by the setting of @DecoderOptions::m_bufferLength.
     * @returns The maximum frames.
     */
    FFFRAMEREADER_EXPORT uint32_t getMaxFrames() noexcept;

    /**
     * Gets a sequence of frames offset from the current stream position using time stamps.
     * @param frameSequence The frame sequence. This is a list of offset times used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3000, 6000} will get the next frame as well as the the frame 3000us
     *  after this and then the frame 3000us after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned. It is only guaranteed that at most @getMaxFrames
     * frames can be returned from a single call to this function.
     */
    FFFRAMEREADER_EXPORT std::vector<std::shared_ptr<Frame>> getNextFrames(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames offset from the current stream position using frame indices.
     * @param frameSequence The frame sequence. This is a list of offset indices used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3, 6} will get the next frame as well as the 3rd frame after this and
     *  then the third frame after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned. It is only guaranteed that at most @getMaxFrames
     * frames can be returned from a single call to this function.
     */
    FFFRAMEREADER_EXPORT std::vector<std::shared_ptr<Frame>> getNextFramesByIndex(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames based on there time stamps
     * @param frameSequence The frame sequence. This is a list of absolute times used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3000, 6000} will get the first frame as well as the the frame 3000us
     *  after this and then the frame 3000us after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned. It is only guaranteed that at most @getMaxFrames
     * frames can be returned from a single call to this function.
     */
    FFFRAMEREADER_EXPORT std::vector<std::shared_ptr<Frame>> getFrames(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Gets a sequence of frames using frame indices.
     * @param frameSequence The frame sequence. This is a list of absolute indices used to specify which frames to
     *  retrieve. e.g. A sequence value of {0, 3, 6} will get the first frame as well as the 3rd frame and then the
     *  third frame after that.
     * @returns A list of frames corresponding to the input sequence, if an error occurred then only the frames
     * retrieved before the error are returned. It is only guaranteed that at most @getMaxFrames
     * frames can be returned from a single call to this function.
     */
    FFFRAMEREADER_EXPORT std::vector<std::shared_ptr<Frame>> getFramesByIndex(
        const std::vector<int64_t>& frameSequence) noexcept;

    /**
     * Query if the stream has reached end of input file.
     * @returns True if end of file, false if not.
     */
    FFFRAMEREADER_EXPORT bool isEndOfFile() const noexcept;

    /**
     * Seeks the stream to the given time stamp. If timestamp does not exactly match a frame then the timestamp rounded
     * to the nearest frame is used instead.
     * @param timeStamp The time stamp to seek to (in micro-seconds).
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_EXPORT bool seek(int64_t timeStamp) noexcept;

    /**
     * Seeks the stream to the given frame number.
     * @param frame The zero-indexed frame number to seek to.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_EXPORT bool seekFrame(int64_t frame) noexcept;

    /**
     * Convert a zero-based frame number to time value represented in microseconds (AV_TIME_BASE).
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time.
     */
    FFFRAMEREADER_EXPORT int64_t frameToTime(int64_t frame) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to a zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted frame index.
     */
    FFFRAMEREADER_EXPORT int64_t timeToFrame(int64_t time) const noexcept;

private:
    std::recursive_mutex m_mutex;

    uint32_t m_bufferLength = 0;                      /**< Length of the ping and pong buffers */
    std::vector<std::shared_ptr<Frame>> m_bufferPing; /**< The primary buffer used to store decoded frames */
    uint32_t m_bufferPingHead =
        0; /**< The position in the ping buffer of the next available frame in the decoded stream */
    std::vector<std::shared_ptr<Frame>> m_bufferPong; /**< The secondary buffer used to store decoded frames */
    std::shared_ptr<Filter> m_filterGraph = nullptr;  /**< The filter graph for optional transformations */
    bool m_outputHost = true; /**< True to output each frame to host CPU memory (only affects hardware decoding) */
    FramePtr m_tempFrame;     /**< The temporary frame used for decoding */

    FormatContextPtr m_formatContext;
    int32_t m_index = -1; /**< Zero-based index of the video stream  */
    CodecContextPtr m_codecContext;

    int64_t m_startTimeStamp = 0;  /**< PTS of the first frame in the stream time base */
    int64_t m_startTimeStamp2 = 0; /**< PTS of the first frame in the codec time base */
    int64_t m_lastDecodedTimeStamp =
        INT64_MIN; /**< The decoder time stamp of the last decoded frame (decoder time base) */
    int64_t m_lastValidTimeStamp =
        INT64_MIN; /**< The decoder time stamp of the last valid stored frame (decoder time base) */
    int64_t m_lastPacketTimeStamp =
        INT64_MIN;                /**< The demuxer time stamp of the last retrieved packet (stream time base) */
    int64_t m_totalFrames = 0;    /**< Stream video duration in frames */
    int64_t m_totalDuration = 0;  /**< Stream video duration in microseconds (AV_TIME_BASE) */
    int64_t m_seekThreshold = 0;  /**< Time stamp difference for determining if a forward seek should forward decode */
    bool m_noBufferFlush = false; /**< True to skip buffer flushing on seeks */
    bool m_frameSeekSupported = true; /**< True if frame seek supported */

    /**
     * Initialises codec parameters needed for future operations.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool initialise() noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to the stream timebase using start time
     * correction.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeToTimeStamp(int64_t time) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to the codec timebase.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeToTimeStamp2(int64_t time) const noexcept;

    /**
     * Convert a stream timebase to a time value represented in microseconds (AV_TIME_BASE) using start time correction.
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted time.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToTime(int64_t timeStamp) const noexcept;

    /**
     * Convert a codec timebase to a time value represented in microseconds (AV_TIME_BASE).
     * @param timeStamp The time stamp represented in the codec internal time base.
     * @return The converted time.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToTime2(int64_t timeStamp) const noexcept;

    /**
     * Convert a zero-based frame number to the stream timebase using start time correction using start time correction.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t frameToTimeStamp(int64_t frame) const noexcept;

    /**
     * Convert a zero-based frame number to the stream timebase.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t frameToTimeStampNoOffset(int64_t frame) const noexcept;

    /**
     * Convert a zero-based frame number to the codec timebase.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t frameToTimeStamp2(int64_t frame) const noexcept;

    /**
     * Convert stream based time stamp to an equivalent zero-based frame number using start time correction.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted frame index.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToFrame(int64_t timeStamp) const noexcept;

    /**
     * Convert stream based time stamp to an equivalent zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted frame index.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToFrameNoOffset(int64_t timeStamp) const noexcept;

    /**
     * Convert codec based time stamp to an equivalent zero-based frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param timeStamp The time stamp represented in the codecs internal time base.
     * @return The converted frame index.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToFrame2(int64_t timeStamp) const noexcept;

    /**
     * Convert a zero-based codec frame number to time value represented in microseconds (AV_TIME_BASE).
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param frame The zero-based frame number
     * @return The converted time.
     */
    FFFRAMEREADER_NO_EXPORT int64_t frameToTime2(int64_t frame) const noexcept;

    /**
     * Convert a time value represented in microseconds (AV_TIME_BASE) to a zero-based codec frame number.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @param time The time in microseconds (AV_TIME_BASE).
     * @return The converted frame index.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeToFrame2(int64_t time) const noexcept;

    /**
     * Convert codec based time stamp to stream based time stamp.
     * @param timeStamp The time stamp represented in the codecs internal time base.
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStamp2ToTimeStamp(int64_t timeStamp) const noexcept;

    /**
     * Convert stream based time stamp to codec based time stamp.
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted time stamp.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToTimeStamp2(int64_t timeStamp) const noexcept;

    /**
     * Convert stream based time stamp to time value represented in microseconds (AV_TIME_BASE).
     * @param timeStamp The time stamp represented in the streams internal time base.
     * @return The converted time.
     */
    FFFRAMEREADER_NO_EXPORT int64_t timeStampToTimeNoOffset(int64_t timeStamp) const noexcept;

    /**
     * Decodes the next block of frames into the pong buffer. Once complete swaps the ping/pong buffers.
     * @param flushTillTime (Optional) All frames with decoder time stamps before this will be discarded.
     * @param seeking       (Optional) True if called directly after seeking.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool decodeNextBlock(int64_t flushTillTime = INT64_MIN, bool seeking = false) noexcept;

    /**
     * Decodes any frames currently pending in the decoder.
     * @param [in,out] flushTillTime All frames with decoder time stamps before this will be discarded.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool decodeNextFrames(int64_t& flushTillTime) noexcept;

    /**
     * Process all buffered with any required additional filtering/conversion.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool processFrames() noexcept;

    /**
     * Process a frame with any required additional filtering/conversion.
     * @param [in,out] frame The frame.
     * @returns True if it succeeds, false if it fails.
     */
    FFFRAMEREADER_NO_EXPORT bool processFrame(FramePtr& frame) const noexcept;

    /**
     * Pops the next available frame from the buffer.
     * @note This requires that peekNextFrame() be called first to ensure there is a valid frame to pop.
     */
    void FFFRAMEREADER_NO_EXPORT popFrame() noexcept;

    /**
     * Return the maximum number of input frames needed by this stream's codec before it can produce output.
     * @note We expect to have to wait this many frames to receive output; any more and a decode stall is detected.
     * @returns The codec delay.
     */
    FFFRAMEREADER_NO_EXPORT int32_t getCodecDelay() const noexcept;

    /**
     * Gets the number of frames that are required in order to perform a seek as opposed to just forward decoding.
     * @returns The seek threshold.
     */
    FFFRAMEREADER_NO_EXPORT int32_t getSeekThreshold() const noexcept;

    /**
     * Return the maximum number of input frames needed by a codec before it can produce output.
     * @param codec The codec.
     * @returns The codec delay.
     */
    FFFRAMEREADER_NO_EXPORT static int32_t GetCodecDelay(const CodecContextPtr& codec) noexcept;

    /**
     * Gets stream start time in the stream timebase.
     * @returns The stream start time.
     */
    FFFRAMEREADER_NO_EXPORT int64_t getStreamStartTime() const noexcept;

    /**
     * Gets total number of frames and the duration of a stream represented in microseconds.
     * @returns The stream frames and duration.
     */
    FFFRAMEREADER_NO_EXPORT std::pair<int64_t, int64_t> getStreamFramesDuration() const noexcept;

    /**
     * Gets the duration of a stream represented in microseconds (AV_TIME_BASE).
     * @returns The duration.
     */
    FFFRAMEREADER_NO_EXPORT int64_t getStreamDuration() const noexcept;
};
} // namespace Ffr
