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
#include "FFFRStream.h"

#include "FFFRDecoderContext.h"
#include "FFFRFilter.h"
#include "FFFRStreamUtils.h"
#include "FFFRUtility.h"
#include "FFFrameReader.h"

#include <algorithm>
#include <string>
using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

namespace Ffr {
Stream::Stream(const std::string& fileName, uint32_t bufferLength, uint32_t seekThreshold, bool noBufferFlush,
    const std::shared_ptr<DecoderContext>& decoderContext, const bool outputHost, Crop crop, Resolution scale,
    PixelFormat format, ConstructorLock) noexcept
{
    // Open the input file
    AVFormatContext* formatPtr = nullptr;
    auto ret = avformat_open_input(&formatPtr, fileName.c_str(), nullptr, nullptr);
    FormatContextPtr tempFormat(formatPtr);
    if (ret < 0) {
        log(("Failed to open input stream: "s += fileName) += ", "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }
    ret = avformat_find_stream_info(tempFormat.get(), nullptr);
    if (ret < 0) {
        log(("Failed finding stream information: "s += fileName) += ", "s += getFfmpegErrorString(ret),
            LogLevel::Error);
        return;
    }

    // Get the primary video stream
    AVCodec* decoder = nullptr;
    ret = av_find_best_stream(tempFormat.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        log(("Failed to find video stream in file: "s += fileName) += ", "s += getFfmpegErrorString(ret),
            LogLevel::Error);
        return;
    }
    AVStream* stream = tempFormat->streams[ret];
    const int32_t index = ret;

    // Validate input parameters
    const auto inHeight = stream->codecpar->height;
    const auto inWidth = stream->codecpar->width;
    bufferLength = std::max(bufferLength, 1u);

    // Check if any scaling/cropping is needed
    Resolution postScale = scale;
    bool cropRequired = (crop.m_top != 0 || crop.m_bottom != 0 || crop.m_left != 0 || crop.m_right != 0);
    if (cropRequired) {
        crop.m_left = std::min(crop.m_left, inWidth - crop.m_right - 1);
        crop.m_top = std::min(crop.m_top, inHeight - crop.m_bottom - 1);
        // Check if scale is actually required after the crop
        const uint32_t width = inWidth - crop.m_left - crop.m_right;
        const uint32_t height = inHeight - crop.m_top - crop.m_bottom;
        if (width == postScale.m_width) {
            postScale.m_width = 0;
        }
        if (height == postScale.m_height) {
            postScale.m_height = 0;
        }
    }
    if (postScale.m_width == static_cast<uint32_t>(inWidth)) {
        postScale.m_width = 0;
    }
    if (postScale.m_height == static_cast<uint32_t>(inHeight)) {
        postScale.m_height = 0;
    }
    bool scaleRequired = (postScale.m_height != 0 || postScale.m_width != 0);

    if (decoderContext.get() != nullptr) {
        if (decoderContext->getType() == DecodeType::Cuda && (cropRequired || scaleRequired)) {
            // Use cuvid decoder instead of nvdec hardware accel
            string cuvidName = decoder->name;
            cuvidName += "_cuvid";
            decoder = avcodec_find_decoder_by_name(cuvidName.c_str());
            if (decoder == nullptr) {
                log("Requested hardware decoding not supported for file: "s += fileName, LogLevel::Error);
                return;
            }
        } else {
            // Check if required codec is supported
            for (int i = 0;; i++) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
                if (config == nullptr) {
                    log(("Decoder does not support device type: "s += decoder->name) +=
                        av_hwdevice_get_type_name(DecoderContext::decodeTypeToFFmpeg(decoderContext->getType())),
                        LogLevel::Error);
                    return;
                }
                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == DecoderContext::decodeTypeToFFmpeg(decoderContext->getType())) {
                    break;
                }
            }
        }
    }

    // Create a decoder context
    CodecContextPtr tempCodec(avcodec_alloc_context3(decoder));
    if (tempCodec.get() == nullptr) {
        log("Failed allocating decoder context: "s += fileName, LogLevel::Error);
        return;
    }

    ret = avcodec_parameters_to_context(tempCodec.get(), stream->codecpar);
    if (ret < 0) {
        log(("Failed copying parameters to decoder context: "s += fileName) += ", "s += getFfmpegErrorString(ret),
            LogLevel::Error);
        return;
    }

    // Ensure codec parameters are correctly setup
    tempCodec->framerate = av_guess_frame_rate(tempFormat.get(), stream, nullptr);
    tempCodec->pkt_timebase = stream->time_base;

    av_opt_set_int(tempCodec.get(), "refcounted_frames", 1, 0);

    // Setup any required hardware decoding parameters
    AVDictionary* opts = nullptr;
    if (decoderContext.get() != nullptr) {
        tempCodec->hw_device_ctx = av_buffer_ref(decoderContext->m_deviceContext.get());
        tempCodec->get_format = decoderContext->getFormatFunction();
        if (tempCodec->get_format == nullptr) {
            log("Hardware Device not properly implemented"s, LogLevel::Error);
            return;
        }
        // Enable extra hardware frames to ensure we don't run out of buffers
        tempCodec->extra_hw_frames = static_cast<int32_t>(bufferLength + 1);
        if (decoderContext->getType() == DecodeType::Cuda && (cropRequired || scaleRequired)) {
            // Use internal cuvid filtering capabilities
            if (scaleRequired) {
                const string resizeString = (to_string(postScale.m_width) += 'x') += to_string(postScale.m_height);
                av_dict_set(&opts, "resize", resizeString.c_str(), 0);
                scaleRequired = false;
            }
            if (cropRequired) {
                const string cropString = (((to_string(crop.m_top) += 'x') += to_string(crop.m_bottom) += 'x') +=
                    to_string(crop.m_left) += 'x') += to_string(crop.m_right);
                av_dict_set(&opts, "crop", cropString.c_str(), 0);
                cropRequired = false;
            }
            const uint32_t surfaces = std::max(getCodecDelay(tempCodec), tempCodec->extra_hw_frames) + 3;
            const string surfacesString = to_string(surfaces);
            av_dict_set(&opts, "surfaces", surfacesString.c_str(), 0);
        }
    } else {
        av_dict_set(&opts, "threads", "auto", 0);
    }
    ret = avcodec_open2(tempCodec.get(), decoder, &opts);
    if (ret < 0) {
        log(("Failed opening decoder: "s += fileName) += ": "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }

    // Check if a filter chain is needed
    const AVPixelFormat inFormat = tempCodec->sw_pix_fmt == AV_PIX_FMT_NONE ?
        static_cast<AVPixelFormat>(tempFormat->streams[index]->codecpar->format) :
        tempCodec->sw_pix_fmt;
    const bool formatRequired =
        (format != PixelFormat::Auto && format != Ffr::getPixelFormat(static_cast<AVPixelFormat>(inFormat)));

    // Check if the pixel format is a known format
    if (Ffr::getPixelFormat(static_cast<AVPixelFormat>(inFormat)) == PixelFormat::Auto) {
        log("Unknown output pixel format, Manual format conversion must be used: "s += fileName, LogLevel::Error);
        return;
    }

    // Add any required filter stages
    if (scaleRequired || cropRequired || formatRequired) {
        // Create a new filter object
        shared_ptr<Filter> filter = make_shared<Filter>(postScale, crop, format, tempFormat, index, tempCodec);

        if (filter->m_filterGraph.get() == nullptr) {
            // filter creation failed
            return;
        }
        m_filterGraph = move(filter);
    }

    // Make the new stream
    m_bufferLength = bufferLength;
    m_outputHost = outputHost && decoderContext.get() != nullptr;
    m_formatContext = move(tempFormat);
    m_index = index;
    m_codecContext = move(tempCodec);
    m_seekThreshold = seekThreshold;
    m_noBufferFlush = noBufferFlush;
    m_frameSeekSupported = m_formatContext->iformat->read_seek2 != nullptr;

    // Ensure ping/pong buffers are long enough to handle the maximum number of frames a video may require
    uint32_t minFrames = std::max(static_cast<uint32_t>(m_seekThreshold), m_bufferLength);

    // Allocate ping and pong buffers
    m_bufferPing.reserve(static_cast<size_t>(minFrames) * 2);
    m_bufferPong.reserve(static_cast<size_t>(minFrames) * 2);

    // Set stream start time and numbers of frames
    m_startTimeStamp = getStreamStartTime();
    m_totalFrames = getStreamFrames();
    m_totalDuration = getStreamDuration();
}

bool Stream::initialise() noexcept
{
    // Decode the first frame (must be done to ensure codec parameters are properly filled)
    const auto backup = m_bufferLength;
    m_bufferLength = 1;
    if (peekNextFrame() == nullptr) {
        m_bufferLength = backup;
        return false;
    }
    m_bufferLength = backup;
    // Check if the first element in the buffer does not match our start time
    const auto startTime = m_bufferPing.front()->getTimeStamp();
    if (startTime != 0) {
        m_startTimeStamp = 0;
        m_startTimeStamp = timeToTimeStamp(startTime);
        // Loop through all current frames and fix the time stamps
        for (auto& i : m_bufferPing) {
            i->m_timeStamp -= startTime;
            i->m_frameNum = timeToFrame2(i->m_timeStamp);
        }
        m_lastDecodedTimeStamp = timeToTimeStamp2(m_bufferPing.back()->getTimeStamp());
        m_lastValidTimeStamp = m_lastDecodedTimeStamp;
    }

    m_seekThreshold = frameToTimeStamp2(m_seekThreshold == 0 ? getSeekThreshold() : m_seekThreshold);
    return true;
}

shared_ptr<Stream> Stream::getStream(const string& fileName, const DecoderOptions& options) noexcept
{
    // Create the device context
    shared_ptr<DecoderContext> deviceContext = nullptr;
    if (options.m_type != DecodeType::Software) {
        deviceContext = make_shared<DecoderContext>(options.m_type, options.m_context, options.m_device);
        if (deviceContext->m_deviceContext.get() == nullptr) {
            // Device creation failed
            return nullptr;
        }
    }

    // Create the new stream
    const bool outputHost = options.m_outputHost && (options.m_type != DecodeType::Software);
    shared_ptr<Stream> stream =
        make_shared<Stream>(fileName, options.m_bufferLength, options.m_seekThreshold, options.m_noBufferFlush,
            deviceContext, outputHost, options.m_crop, options.m_scale, options.m_format, ConstructorLock());
    if (stream->m_codecContext.get() == nullptr) {
        // Stream creation failed
        return nullptr;
    }

    // Initialise stream data
    if (!stream->initialise()) {
        return nullptr;
    }

    return stream;
}

uint32_t Stream::getWidth() const noexcept
{
    if (m_filterGraph.get() != nullptr) {
        return m_filterGraph->getWidth();
    }
    return m_codecContext->width;
}

uint32_t Stream::getHeight() const noexcept
{
    if (m_filterGraph.get() != nullptr) {
        return m_filterGraph->getHeight();
    }
    return m_codecContext->height;
}

double Stream::getAspectRatio() const noexcept
{
    const auto sampleRatio = StreamUtils::getSampleAspectRatio(this);
    if (sampleRatio.num != 0) {
        return av_q2d(av_mul_q(av_make_q(getWidth(), getHeight()), sampleRatio));
    }
    return static_cast<double>(getWidth()) / static_cast<double>(getHeight());
}

PixelFormat Stream::getPixelFormat() const noexcept
{
    return Ffr::getPixelFormat(StreamUtils::getPixelFormat(this));
}

int64_t Stream::getTotalFrames() const noexcept
{
    return m_totalFrames;
}

int64_t Stream::getDuration() const noexcept
{
    return m_totalDuration;
}

double Stream::getFrameRate() const noexcept
{
    return av_q2d(StreamUtils::getFrameRate(this));
}

uint32_t Stream::getFrameSize() const noexcept
{
    return av_image_get_buffer_size(StreamUtils::getPixelFormat(this), getWidth(), getHeight(), 32);
}

DecodeType Stream::getDecodeType() const noexcept
{
    if (m_codecContext->pix_fmt == AV_PIX_FMT_CUDA) {
        return DecodeType::Cuda;
    }
    return DecodeType::Software;
}

shared_ptr<Frame> Stream::peekNextFrame() noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    // Check if we actually have any frames in the current buffer
    if (m_bufferPingHead >= m_bufferPing.size()) {
        // TODO: Async decode of next block, should start once reached the last couple of frames in a buffer
        // The swap buffer only should occur when ping buffer is exhausted and pong decode has completed
        if (!decodeNextBlock()) {
            return nullptr;
        }
        // Check if there are any new frames or we reached EOF
        if (m_bufferPing.size() == 0) {
            log("Cannot get a new frame, End of file has been reached"s, LogLevel::Warning);
            return nullptr;
        }
    }
    // Get frame from ping buffer
    return m_bufferPing[m_bufferPingHead];
}

shared_ptr<Frame> Stream::getNextFrame() noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    auto ret = peekNextFrame();
    if (ret != nullptr) {
        // Remove the frame from list
        popFrame();
    }
    return ret;
}

uint32_t Stream::getMaxFrames() noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex); // Lock in case buffer length is being modified by other function calls
    return m_bufferLength;
}

vector<std::shared_ptr<Frame>> Stream::getNextFrames(const vector<int64_t>& frameSequence) noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    const auto startTime = m_bufferPingHead < m_bufferPing.size() ?
        m_bufferPing.front()->getTimeStamp() :
        timeStampToTime2(m_lastDecodedTimeStamp) + frameToTime2(1);
    vector<int64_t> newSequence;
    generate_n(back_inserter(newSequence), frameSequence.size(),
        [it = frameSequence.begin(), startTime]() mutable { return *(it++) + startTime; });
    return getFrames(newSequence);
}

vector<shared_ptr<Frame>> Stream::getNextFramesByIndex(const vector<int64_t>& frameSequence) noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    vector<shared_ptr<Frame>> ret;
    const auto startFrame = m_bufferPingHead < m_bufferPing.size() ? m_bufferPing.front()->getFrameNumber() :
                                                                     timeStampToFrame2(m_lastDecodedTimeStamp) + 1;
    vector<int64_t> newSequence;
    generate_n(back_inserter(newSequence), frameSequence.size(),
        [it = frameSequence.begin(), startFrame]() mutable { return *(it++) + startFrame; });
    return getFramesByIndex(newSequence);
}

vector<std::shared_ptr<Frame>> Stream::getFrames(const vector<int64_t>& frameSequence) noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    vector<shared_ptr<Frame>> ret;
    const auto bufferBackup = m_bufferLength;
    for (auto i = frameSequence.cbegin(); i < frameSequence.cend(); ++i) {
        // Max number of frames that can be reliable held at any point in time is equal to buffer length
        if (ret.size() >= bufferBackup) {
            break;
        }
        if (m_bufferPingHead >= m_bufferPing.size()) {
            // Set buffer length based on remaining frames
            int64_t maxFound = *i;
            for (auto j = i + 1; j < frameSequence.cend(); ++j) {
                const auto range = timeToFrame(*j - *i);
                if (range < m_seekThreshold &&
                    range < static_cast<int64_t>(bufferBackup) - static_cast<int64_t>(ret.size())) {
                    maxFound = *j;
                } else {
                    break;
                }
            }
            m_bufferLength = std::max(static_cast<uint32_t>(timeToFrame(maxFound - *i)), uint32_t{1});
        }
        // Use seek function as that will determine if seek or just a forward decode is needed
        if (!seek(*i)) {
            break;
        }
        auto frame = getNextFrame();
        if (frame == nullptr) {
            break;
        }
        ret.emplace_back(move(frame));
    }
    m_bufferLength = bufferBackup;
    return ret;
}

vector<std::shared_ptr<Frame>> Stream::getFramesByIndex(const vector<int64_t>& frameSequence) noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    vector<shared_ptr<Frame>> ret;
    const auto bufferBackup = m_bufferLength;
    for (auto i = frameSequence.cbegin(); i < frameSequence.cend(); ++i) {
        // Max number of frames that can be reliable held at any point in time is equal to buffer length
        if (ret.size() >= bufferBackup) {
            break;
        }
        if (m_bufferPingHead >= m_bufferPing.size()) {
            // Set buffer length based on remaining frames
            int64_t maxFound = *i;
            for (auto j = i + 1; j < frameSequence.cend(); ++j) {
                const auto range = *j - *i;
                if (range < m_seekThreshold &&
                    range < static_cast<int64_t>(bufferBackup) - static_cast<int64_t>(ret.size())) {
                    maxFound = *j;
                } else {
                    break;
                }
            }
            m_bufferLength = std::max(static_cast<uint32_t>(timeToFrame(maxFound - *i)), uint32_t{1});
        }
        // Use seek function as that will determine if seek or just a forward decode is needed
        if (!seekFrame(*i)) {
            break;
        }
        auto frame = getNextFrame();
        if (frame == nullptr) {
            break;
        }
        ret.emplace_back(move(frame));
    }
    m_bufferLength = bufferBackup;
    return ret;
}

bool Stream::isEndOfFile() const noexcept
{
    return timeStampToFrame2(m_lastDecodedTimeStamp) + 1 >= getTotalFrames();
}

bool Stream::seek(const int64_t timeStamp) noexcept
{
    if (timeStamp >= getDuration() || timeStamp < 0) {
        // Bail if seek is not possible
        return false;
    }

    lock_guard<recursive_mutex> lock(m_mutex);
    // Check if we actually have any frames in the current buffer
    if (m_bufferPingHead < m_bufferPing.size()) {
        // Check if the frame is in the current buffer
        const auto halfFrameDuration = frameToTime(1) / 2;
        if ((timeStamp >= m_bufferPing[m_bufferPingHead]->getTimeStamp() - halfFrameDuration) &&
            (timeStamp < m_bufferPing.back()->getTimeStamp() + halfFrameDuration)) {
            // Dump all frames before requested one
            while (true) {
                // Get next frame
                auto ret = peekNextFrame();
                if (ret == nullptr) {
                    return false;
                }
                // Check if we have found our requested time stamp or it is within the timestamp range of the next frame
                if (timeStamp < (ret->getTimeStamp() + halfFrameDuration)) {
                    break;
                }
                // Remove frames from ping buffer
                popFrame();
            }
            return true;
        }
    }

    // Check if this is a forward seek within some predefined small range. If so then just continue reading
    // packets from the current position into buffer.
    const auto timeStamp2 = timeToTimeStamp2(timeStamp);
    if (timeStamp2 > m_lastDecodedTimeStamp) {
        // Forward decode if within some predefined range of existing point.
        const auto timeStep = timeStamp2 - m_lastDecodedTimeStamp;
        if (timeStep <= m_seekThreshold) {
            // Loop through until the requested timestamp is found (or nearest timestamp rounded up if exact match
            // could not be found). Discard all frames occuring before timestamp

            // Decode the next block of frames
            return decodeNextBlock(timeStamp2);
        }
    }

    // Seek to desired timestamp
    const auto localTimeStamp = timeToTimeStamp(timeStamp);
    const auto err = avformat_seek_file(m_formatContext.get(), m_index,
        localTimeStamp - timeStamp2ToTimeStamp(m_seekThreshold), localTimeStamp, localTimeStamp, 0);
    if (err < 0) {
        log("Failed seeking to specified time stamp "s += to_string(timeStamp) += getFfmpegErrorString(err),
            LogLevel::Error);
        return false;
    }

    // Decode the next block of frames
    return decodeNextBlock(timeStamp2, true);
}

bool Stream::seekFrame(const int64_t frame) noexcept
{
    if (frame >= getTotalFrames()) {
        // Early out if seek is not possible
        return false;
    }

    lock_guard<recursive_mutex> lock(m_mutex);
    // Check if we actually have any frames in the current buffer
    if (m_bufferPingHead < m_bufferPing.size()) {
        // Check if the frame is in the current buffer
        if ((frame >= m_bufferPing[m_bufferPingHead]->getFrameNumber()) &&
            (frame <= m_bufferPing.back()->getFrameNumber())) {
            // Dump all frames before requested one
            while (true) {
                // Get next frame
                auto ret = peekNextFrame();
                if (ret == nullptr) {
                    return false;
                }
                // Check if we have found our requested frame
                if (frame <= ret->getFrameNumber()) {
                    break;
                }
                // Remove frames from ping buffer
                popFrame();
            }
            return true;
        }
    }

    // Check if this is a forward seek within some predefined small range. If so then just continue reading
    // packets from the current position into buffer.
    const auto timeStamp2 = frameToTimeStamp2(frame);
    if (frame > m_lastDecodedTimeStamp) {
        // Forward decode if within some predefined range of existing point.
        const auto timeStep = timeStamp2 - m_lastDecodedTimeStamp;
        if (timeStep <= m_seekThreshold) {
            // Loop through until the requested timestamp is found (or nearest timestamp rounded up if exact match
            // could not be found). Discard all frames occuring before timestamp

            // Decode the next block of frames
            return decodeNextBlock(timeStamp2);
        }
    }

    if (!m_frameSeekSupported) {
        return seek(frameToTime(frame));
    }
    // Seek to desired timestamp
    const auto frameInternal = frame + timeStampToFrameNoOffset(m_startTimeStamp);
    const auto err = avformat_seek_file(m_formatContext.get(), m_index,
        frameInternal - timeStampToFrame2(m_seekThreshold), frameInternal, frameInternal, AVSEEK_FLAG_FRAME);
    if (err < 0) {
        log("Failed to seek to specified frame "s += to_string(frame) += ": "s += getFfmpegErrorString(err),
            LogLevel::Error);
        return false;
    }

    // Decode the next block of frames
    return decodeNextBlock(timeStamp2, true);
}

int64_t Stream::frameToTime(const int64_t frame) const noexcept
{
    return av_rescale_q(frame, av_make_q(AV_TIME_BASE, 1), m_formatContext->streams[m_index]->r_frame_rate);
}

int64_t Stream::timeToFrame(const int64_t time) const noexcept
{
    return av_rescale_q(time, av_make_q(1, AV_TIME_BASE), av_inv_q(m_formatContext->streams[m_index]->r_frame_rate));
}

int64_t Stream::timeToTimeStamp(const int64_t time) const noexcept
{
    static_assert(AV_TIME_BASE == 1000000, "FFmpeg internal time_base does not match expected value");
    // Rescale a timestamp that is stored in microseconds (AV_TIME_BASE) to the stream timebase
    return m_startTimeStamp +
        av_rescale_q(time, av_make_q(1, AV_TIME_BASE), m_formatContext->streams[m_index]->time_base);
}

int64_t Stream::timeToTimeStamp2(const int64_t time) const noexcept
{
    return av_rescale_q(time, av_make_q(1, AV_TIME_BASE), m_codecContext->time_base);
}

int64_t Stream::timeStampToTime(const int64_t timeStamp) const noexcept
{
    // Perform opposite operation to timeToTimeStamp
    return av_rescale_q(
        timeStamp - m_startTimeStamp, m_formatContext->streams[m_index]->time_base, av_make_q(1, AV_TIME_BASE));
}

int64_t Stream::timeStampToTime2(const int64_t timeStamp) const noexcept
{
    // Perform opposite operation to timeToTimeStamp
    return av_rescale_q(timeStamp, m_codecContext->time_base, av_make_q(1, AV_TIME_BASE));
}

int64_t Stream::frameToTimeStamp(const int64_t frame) const noexcept
{
    return m_startTimeStamp +
        av_rescale_q(frame, av_inv_q(m_formatContext->streams[m_index]->r_frame_rate),
            m_formatContext->streams[m_index]->time_base);
}

int64_t Stream::frameToTimeStampNoOffset(const int64_t frame) const noexcept
{
    return av_rescale_q(
        frame, av_inv_q(m_formatContext->streams[m_index]->r_frame_rate), m_formatContext->streams[m_index]->time_base);
}

int64_t Stream::frameToTimeStamp2(const int64_t frame) const noexcept
{
    return av_rescale_q(frame, av_inv_q(m_codecContext->framerate), m_codecContext->time_base);
}

int64_t Stream::timeStampToFrame(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp - m_startTimeStamp, m_formatContext->streams[m_index]->time_base,
        av_inv_q(m_formatContext->streams[m_index]->r_frame_rate));
}

int64_t Stream::timeStampToFrameNoOffset(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp, m_formatContext->streams[m_index]->time_base,
        av_inv_q(m_formatContext->streams[m_index]->r_frame_rate));
}

int64_t Stream::timeStampToFrame2(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp, m_codecContext->time_base, av_inv_q(m_codecContext->framerate));
}

int64_t Stream::frameToTime2(const int64_t frame) const noexcept
{
    return av_rescale_q(frame, av_make_q(AV_TIME_BASE, 1), m_codecContext->framerate);
}

int64_t Stream::timeToFrame2(const int64_t time) const noexcept
{
    return av_rescale_q(time, av_make_q(1, AV_TIME_BASE), av_inv_q(m_codecContext->framerate));
}

int64_t Stream::timeStamp2ToTimeStamp(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp, m_codecContext->time_base, m_formatContext->streams[m_index]->time_base);
}

bool Stream::decodeNextBlock(int64_t flushTillTime, bool seeking) noexcept
{
    // TODO: If we are using async decode then this needs to just return if a decode is already running

    // Clean out current buffer and release any frames it may still hold
    m_bufferPing.resize(0);
    m_bufferPingHead = 0;

    // Decode the next buffer sequence
    AVPacket packet;
    av_init_packet(&packet);
    bool eof = false;
    do {
        // This may or may not be a keyframe, So we just start decoding packets until we receive a valid frame
        auto ret = av_read_frame(m_formatContext.get(), &packet);
        bool sentPacket = false;
        if (ret < 0) {
            if (ret != AVERROR_EOF) {
                av_packet_unref(&packet);
                log("Failed to retrieve new frame: "s += getFfmpegErrorString(ret), LogLevel::Error);
                return false;
            }
            eof = true;
            // Send flush packet to decoder
            avcodec_send_packet(m_codecContext.get(), nullptr);
            sentPacket = true;
        } else if (m_index == packet.stream_index) {
            if (seeking) {
                seeking = false;
                // Check if a seek went backwards when it should have gone forward
                auto packetTimeStamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
                if (flushTillTime > m_lastDecodedTimeStamp && m_lastPacketTimeStamp > packetTimeStamp) {
                    while (m_lastPacketTimeStamp > packetTimeStamp || m_index != packet.stream_index) {
                        av_packet_unref(&packet);
                        ret = av_read_frame(m_formatContext.get(), &packet);
                        if (ret < 0) {
                            av_packet_unref(&packet);
                            log("Failed to retrieve new frame: "s += getFfmpegErrorString(ret), LogLevel::Error);
                            return false;
                        }
                        packetTimeStamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
                    }
                    av_packet_unref(&packet);
                    continue;
                }
                if (!m_noBufferFlush) {
                    avcodec_flush_buffers(m_codecContext.get());
                    m_lastDecodedTimeStamp = -1;
                }
                m_lastValidTimeStamp = -1;
            }
            m_lastPacketTimeStamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
            // Convert timebase
            av_packet_rescale_ts(&packet, m_formatContext->streams[m_index]->time_base, m_codecContext->time_base);
            ret = avcodec_send_packet(m_codecContext.get(), &packet);
            while (ret < 0) {
                if (ret == AVERROR_EOF) {
                    avcodec_flush_buffers(m_codecContext.get());
                    ret = avcodec_send_packet(m_codecContext.get(), &packet);
                } else if (ret == AVERROR(EAGAIN)) {
                    if (!decodeNextFrames(flushTillTime)) {
                        return false;
                    }
                    ret = avcodec_send_packet(m_codecContext.get(), &packet);
                    if (ret == AVERROR(EAGAIN)) {
                        av_packet_unref(&packet);
                        log("Failed to send packet to decoder: "s += getFfmpegErrorString(ret), LogLevel::Error);
                        return false;
                    }
                } else {
                    av_packet_unref(&packet);
                    log("Failed to send packet to decoder: "s += getFfmpegErrorString(ret), LogLevel::Error);
                    return false;
                }
            }
            sentPacket = true;
        }
        av_packet_unref(&packet);

        if (sentPacket) {
            // Decode any pending frames
            if (!decodeNextFrames(flushTillTime)) {
                return false;
            }
        }

        // TODO: The maximum number of frames that are needed to get a valid frame is calculated using getCodecDelay().
        // If more than that are passed without a returned frame then an error has occured (ignoring flushTillTime).
    } while (m_bufferPong.size() < m_bufferLength && !eof);

    if (eof) {
        // Check if we got more frames than we should have. This occurs when there are missing frames that are
        // padded in resulting in more output frames than expected.
        while (!m_bufferPong.empty()) {
            if (m_bufferPong.back()->getTimeStamp() < this->getDuration() &&
                m_bufferPong.back()->getTimeStamp() != AV_NOPTS_VALUE) {
                break;
            }
            m_bufferPong.pop_back();
        }
    }
    // Swap ping and pong buffer
    swap(m_bufferPing, m_bufferPong);

    return true;
}

bool Stream::decodeNextFrames(int64_t& flushTillTime) noexcept
{
    // Loop through and retrieve all decoded frames
    do {
        if (*m_tempFrame == nullptr) {
            m_tempFrame = FramePtr(av_frame_alloc());
            if (*m_tempFrame == nullptr) {
                log("Failed to allocate new frame"s, LogLevel::Error);
                return false;
            }
        }
        const auto ret = avcodec_receive_frame(m_codecContext.get(), *m_tempFrame);
        if (ret < 0) {
            if ((ret == AVERROR(EAGAIN)) || (ret == AVERROR_EOF)) {
                return true;
            }
            log("Failed to receive decoded frame: "s += getFfmpegErrorString(ret), LogLevel::Error);
            return false;
        }

        // Calculate time stamp for frame
        if (m_tempFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
            // Some decoders (looking at you cuvid) don't return a best_effort_timestamp.
            // Based on ffmpegs decode.c guess_correct_pts
            if (m_tempFrame->pkt_dts != AV_NOPTS_VALUE) {
                m_codecContext->pts_correction_num_faulty_dts +=
                    m_tempFrame->pkt_dts <= m_codecContext->pts_correction_last_dts;
                m_codecContext->pts_correction_last_dts = m_tempFrame->pkt_dts;
            } else if (m_tempFrame->pts != AV_NOPTS_VALUE) {
                m_codecContext->pts_correction_last_dts = m_tempFrame->pts;
            }

            if (m_tempFrame->pts != AV_NOPTS_VALUE) {
                m_codecContext->pts_correction_num_faulty_pts +=
                    m_tempFrame->pts <= m_codecContext->pts_correction_last_pts;
                m_codecContext->pts_correction_last_pts = m_tempFrame->pts;
            } else if (m_tempFrame->pkt_dts != AV_NOPTS_VALUE) {
                m_codecContext->pts_correction_last_pts = m_tempFrame->pkt_dts;
            }

            if ((m_codecContext->pts_correction_num_faulty_pts <= m_codecContext->pts_correction_num_faulty_dts ||
                    m_tempFrame->pkt_dts == AV_NOPTS_VALUE) &&
                m_tempFrame->pts != AV_NOPTS_VALUE) {
                m_tempFrame->best_effort_timestamp = m_tempFrame->pts;
            } else {
                m_tempFrame->best_effort_timestamp = m_tempFrame->pkt_dts;
            }
        }
        int64_t offsetTimeStamp = m_tempFrame->best_effort_timestamp;
        if (offsetTimeStamp == AV_NOPTS_VALUE) {
            // Try and just rebuild it from the previous frame
            offsetTimeStamp = m_lastDecodedTimeStamp + frameToTimeStamp2(1);
        } else if (m_startTimeStamp != 0) {
            // Remove the start time from calculations
            // TODO: precalculate this
            offsetTimeStamp -=
                av_rescale_q(m_startTimeStamp, m_formatContext->streams[m_index]->time_base, m_codecContext->time_base);
        }

        // Store last decoded time stamp
        auto previousDecodedTimeStamp = m_lastDecodedTimeStamp;
        m_lastDecodedTimeStamp = offsetTimeStamp;

        if (flushTillTime >= 0) {
            const auto singleFrame = frameToTimeStamp2(1);
            const auto doubleTime =
                offsetTimeStamp * 2; // Use double in cases where singleFrame cannot be halved without losses
            const auto doubleFlush = flushTillTime * 2;
            if (doubleFlush <= (doubleTime - singleFrame) || doubleFlush > (doubleTime + singleFrame)) {
                if (previousDecodedTimeStamp < flushTillTime && offsetTimeStamp > flushTillTime) {
                    // This requires a duplicate frame
                    previousDecodedTimeStamp = flushTillTime - singleFrame;
                } else {
                    // Dump this frame and continue
                    av_frame_unref(*m_tempFrame);
                    continue;
                }
            } else {
                // Prevent duplicating frames if this is the first valid frame that is being used after a flush
                previousDecodedTimeStamp = -1;
            }
            flushTillTime = -1;
        } else if (offsetTimeStamp < previousDecodedTimeStamp ||
            (m_lastValidTimeStamp != -1 && offsetTimeStamp <= m_lastValidTimeStamp)) {
            // Dump this frame and continue
            av_frame_unref(*m_tempFrame);
            continue;
        }

        m_lastValidTimeStamp = offsetTimeStamp;
        auto timeStamp = timeStampToTime2(offsetTimeStamp);
        auto frameNum = timeStampToFrame2(offsetTimeStamp);

        // Check if we have skipped a frame
        if (previousDecodedTimeStamp != -1) {
            const auto previous = timeStampToFrame2(previousDecodedTimeStamp);
            if (frameNum != previous + 1) {
                // Fill in missing frames by duplicating the old one
                auto fillFrameNum = previous;
                int64_t fillTimeStamp;
                for (auto i = previous + 1; i < frameNum; i++) {
                    ++fillFrameNum;
                    fillTimeStamp = frameToTime2(fillFrameNum);
                    FramePtr frameClone(av_frame_clone(*m_tempFrame));
                    // Perform any required filtering
                    if (!processFrame(frameClone)) {
                        return false;
                    }
                    if (frameClone->height != 0) {
                        m_bufferPong.emplace_back(make_shared<Frame>(
                            frameClone, fillTimeStamp, fillFrameNum, m_formatContext, m_codecContext));
                    }
                }
            }
        }

        // Perform any required filtering
        if (!processFrame(m_tempFrame)) {
            return false;
        }

        if (m_tempFrame->height != 0) {
            // Add the new frame to the pong buffer
            m_bufferPong.emplace_back(
                make_shared<Frame>(m_tempFrame, timeStamp, frameNum, m_formatContext, m_codecContext));
        }
    } while (m_bufferPong.size() < m_bufferLength);
    return true;
}

bool Stream::processFrame(FramePtr& frame) const noexcept
{
    if (m_filterGraph != nullptr) {
        // Update internal timestamps to ensure they are valid
        frame->best_effort_timestamp = m_lastValidTimeStamp;
        frame->pts = frame->best_effort_timestamp;
        StreamUtils::rescale(frame, m_codecContext->time_base, av_buffersink_get_time_base(m_filterGraph->m_sink));
        if (!m_filterGraph->sendFrame(frame)) {
            av_frame_unref(*frame);
            return false;
        }
        if (!m_filterGraph->receiveFrame(frame)) {
            av_frame_unref(*frame);
            return false;
        }
        // Check if we actually got a new frame or we need to continue
        if (frame->height == 0) {
            return true;
        }
    }

    // Check type of memory pointer requested and perform a memory move
    if (m_outputHost) {
        // TODO: need some sort of buffer pool
        FramePtr frame2(av_frame_alloc());
        if (*frame2 == nullptr) {
            av_frame_unref(*frame);
            log("Failed to allocate new host frame"s, LogLevel::Error);
            return false;
        }
        const auto ret2 = av_hwframe_transfer_data(*frame2, *frame, 0);
        av_frame_unref(*frame);
        if (ret2 < 0) {
            av_frame_unref(*frame2);
            log("Failed to copy frame to host: "s += getFfmpegErrorString(ret2), LogLevel::Error);
            return false;
        }
        frame = move(frame2);
    }
    return true;
}

void Stream::popFrame() noexcept
{
    if (m_bufferPingHead >= m_bufferPing.size()) {
        log("No more frames to pop"s, LogLevel::Error);
        return;
    }
    // Release reference and pop frame
    m_bufferPing[m_bufferPingHead++].reset();
}

int32_t Stream::getCodecDelay() const noexcept
{
    return getCodecDelay(m_codecContext);
}

int32_t Stream::getSeekThreshold() const noexcept
{
    // This value should be optimised based on the GOP length and decoding cost of the input video
    // Using the test files obtains the following ideal threshold values
    // 64 for 0 r=4, b=2, d=0  gop=250
    //  5 for 1 r=4, b=2, d=0  gop=5
    //  6 for 4 r=1, b=0, d=0  gop=5
    //  8 for 6 r=2, b=1, d=0  gop=12
    // 10 for 7 r=2, b=1, d=0  gop=24
    //  8 for 8 r=2, b=1, d=0  gop=6
    // = (219.1647g)/(34.49228 + g)
    // Since we dont actually have access to the gop size we have to guess an estimate
    const auto gop = 2 * static_cast<float>(m_codecContext->has_b_frames) +
        1.1f * expf(1.298964f * static_cast<float>(m_codecContext->refs));
    const auto frames = -4.523664f + 10.42266f * expf(0.01506527f * gop);
    return static_cast<int32_t>(frames);
}

int32_t Stream::getCodecDelay(const CodecContextPtr& codec) noexcept
{
    return std::max(((codec->codec->capabilities & AV_CODEC_CAP_DELAY) ? codec->delay : 0) + codec->has_b_frames, 1);
}

int64_t Stream::getStreamStartTime() const noexcept
{
    // First check if the stream has a start timeStamp
    AVStream* stream = m_formatContext->streams[m_index];
    if (stream->start_time != int64_t(AV_NOPTS_VALUE)) {
        return stream->start_time;
    }
    // Seek to the first frame in the video to get information directly from it
    avcodec_flush_buffers(m_codecContext.get());
    int64_t startDts = 0LL;
    if (stream->first_dts != int64_t(AV_NOPTS_VALUE)) {
        startDts = std::min(startDts, stream->first_dts);
    }
    if (av_seek_frame(m_formatContext.get(), m_index, startDts, AVSEEK_FLAG_BACKWARD) < 0) {
        log("Failed to determine stream start time"s, LogLevel::Error);
        return 0;
    }
    AVPacket packet;
    av_init_packet(&packet);
    // Read frames until we get one for the video stream that contains a valid PTS or DTS.
    auto startTimeStamp = int64_t(AV_NOPTS_VALUE);
    const auto maxPackets = getCodecDelay();
    // Loop through multiple packets to take into account b-frame reordering issues
    for (int32_t i = 0; i < maxPackets;) {
        if (av_read_frame(m_formatContext.get(), &packet) < 0) {
            return 0;
        }
        if (packet.stream_index == m_index) {
            // Get the Presentation time stamp for the packet, if this value is not set then try the Decompression time
            // stamp
            auto pts = packet.pts;
            if (pts == int64_t(AV_NOPTS_VALUE)) {
                pts = packet.dts;
            }
            if ((pts != int64_t(AV_NOPTS_VALUE)) &&
                ((pts < startTimeStamp) || (startTimeStamp == int64_t(AV_NOPTS_VALUE)))) {
                startTimeStamp = pts;
            }
            ++i;
        }
        av_packet_unref(&packet);
    }
    // Seek back to start of file so future reads continue back at start
    av_seek_frame(m_formatContext.get(), m_index, startDts, AVSEEK_FLAG_BACKWARD);
    return (startTimeStamp != int64_t(AV_NOPTS_VALUE)) ? startTimeStamp : 0;
}

int64_t Stream::getStreamFrames() const noexcept
{
    AVStream* stream = m_formatContext->streams[m_index];
    // Check if the number of frames is specified in the stream
    if (stream->nb_frames > 0) {
        return stream->nb_frames - timeStampToFrame(m_startTimeStamp * 2);
    }

    // Attempt to calculate from stream duration, time base and fps
    if (stream->duration > 0) {
        return timeStampToFrame(int64_t(stream->duration));
    }

    // If we are at this point then the only option is to scan the entire file and check the DTS/PTS.
    int64_t foundTimeStamp = m_startTimeStamp;

    // Seek last key-frame.
    avcodec_flush_buffers(m_codecContext.get());
    const auto maxSeek = frameToTimeStampNoOffset(1UL << 29UL);
    if (avformat_seek_file(m_formatContext.get(), m_index, INT64_MIN, maxSeek, maxSeek, 0) < 0) {
        log("Failed to determine number of frames in stream"s, LogLevel::Error);
        return 0;
    }

    // Read up to last frame, extending max PTS for every valid PTS value found for the video stream.
    AVPacket packet;
    av_init_packet(&packet);
    while (av_read_frame(m_formatContext.get(), &packet) >= 0) {
        if (packet.stream_index == m_index) {
            const auto found = packet.pts != AV_NOPTS_VALUE ? packet.pts : packet.dts;
            if (found > foundTimeStamp) {
                foundTimeStamp = found;
            }
        }
        av_packet_unref(&packet);
    }

    // Seek back to start of file so future reads continue back at start
    int64_t startDts = 0LL;
    if (stream->first_dts != int64_t(AV_NOPTS_VALUE)) {
        startDts = std::min(startDts, stream->first_dts);
    }
    av_seek_frame(m_formatContext.get(), m_index, startDts, AVSEEK_FLAG_BACKWARD);

    // The detected value is the index of the last frame plus one
    return timeStampToFrame(foundTimeStamp) + 1;
}

int64_t Stream::getStreamDuration() const noexcept
{
    // First try and get the format duration if specified. For some formats this durations can override the duration
    // specified within each stream which is why it should be checked first.
    AVStream* stream = m_formatContext->streams[m_index];
    if (m_formatContext->duration > 0) {
        return m_formatContext->duration -
            timeStampToTime(m_startTimeStamp * 2); //*2 To avoid the minus in timeStampToTime
    }

    // Check if the duration is specified in the stream
    if (stream->duration > 0) {
        return timeStampToTime(stream->duration);
    }

    // If we are at this point then the only option is to scan the entire file and check the DTS/PTS.
    int64_t foundTimeStamp = m_startTimeStamp;

    // Seek last key-frame.
    avcodec_flush_buffers(m_codecContext.get());
    const auto maxSeek = frameToTimeStampNoOffset(1UL << 29UL);
    if (avformat_seek_file(m_formatContext.get(), m_index, INT64_MIN, maxSeek, maxSeek, 0) < 0) {
        log("Failed to determine stream duration"s, LogLevel::Error);
        return 0;
    }

    // Read up to last frame, extending max PTS for every valid PTS value found for the video stream.
    AVPacket packet;
    av_init_packet(&packet);
    while (av_read_frame(m_formatContext.get(), &packet) >= 0) {
        if (packet.stream_index == m_index) {
            const auto found = packet.pts != AV_NOPTS_VALUE ? packet.pts : packet.dts;
            if (found > foundTimeStamp) {
                foundTimeStamp = found;
            }
        }
        av_packet_unref(&packet);
    }

    // Seek back to start of file so future reads continue back at start
    int64_t startDts = 0LL;
    if (stream->first_dts != int64_t(AV_NOPTS_VALUE)) {
        startDts = std::min(startDts, stream->first_dts);
    }
    av_seek_frame(m_formatContext.get(), m_index, startDts, AVSEEK_FLAG_BACKWARD);

    // The detected value is timestamp of the last detected packet plus the duration of that frame
    return timeStampToTime(foundTimeStamp) + frameToTime(1);
}
} // namespace Ffr