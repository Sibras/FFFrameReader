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
Stream::Stream(const std::string& fileName, uint32_t bufferLength, const uint32_t seekThreshold, bool noBufferFlush,
    const std::shared_ptr<DecoderContext>& decoderContext, const bool outputHost, Crop crop, const Resolution scale,
    const PixelFormat format, ConstructorLock) noexcept
{
    // Open the input file
    AVFormatContext* formatPtr = nullptr;
    auto ret = avformat_open_input(&formatPtr, fileName.c_str(), nullptr, nullptr);
    FormatContextPtr tempFormat(formatPtr);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed to open input stream: ", fileName, ", ", getFfmpegErrorString(ret));
        return;
    }
    ret = avformat_find_stream_info(tempFormat.get(), nullptr);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed finding stream information: ", fileName, ", ", getFfmpegErrorString(ret));
        return;
    }

    // Get the primary video stream
    AVCodec* decoder = nullptr;
    ret = av_find_best_stream(tempFormat.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        logInternal(
            LogLevel::Error, "Failed to find video stream in file: ", fileName, ", ", getFfmpegErrorString(ret));
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
                logInternal(LogLevel::Error, "Requested hardware decoding not supported for file: ", fileName);
                return;
            }
            noBufferFlush = false; // Cant use fast seek with the older cuvid decoder
            logInternal(LogLevel::Info, "Stream- Using decoder: cuvid");
        } else {
            // Check if required codec is supported
            for (int i = 0;; i++) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
                if (config == nullptr) {
                    logInternal(LogLevel::Error, "Decoder does not support device type: ", decoder->name,
                        av_hwdevice_get_type_name(DecoderContext::DecodeTypeToFFmpeg(decoderContext->getType())));
                    return;
                }
                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == DecoderContext::DecodeTypeToFFmpeg(decoderContext->getType())) {
                    break;
                }
            }
            logInternal(LogLevel::Info, "Stream- Using decoder: ", decoder->name);
        }
    }

    // Create a decoder context
    CodecContextPtr tempCodec(avcodec_alloc_context3(decoder));
    if (tempCodec.get() == nullptr) {
        logInternal(LogLevel::Error, "Failed allocating decoder context: ", fileName);
        return;
    }

    ret = avcodec_parameters_to_context(tempCodec.get(), stream->codecpar);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed copying parameters to decoder context: ", fileName, ", ",
            getFfmpegErrorString(ret));
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
            logInternal(LogLevel::Error, "Hardware Device not properly implemented");
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
                logInternal(
                    LogLevel::Info, "Stream- Using cuvid resizing: ", postScale.m_width, ", ", postScale.m_height);
            }
            if (cropRequired) {
                const string cropString = (((to_string(crop.m_top) += 'x') += to_string(crop.m_bottom) += 'x') +=
                    to_string(crop.m_left) += 'x') += to_string(crop.m_right);
                av_dict_set(&opts, "crop", cropString.c_str(), 0);
                cropRequired = false;
                logInternal(LogLevel::Info, "Stream- Using cuvid cropping: ", crop.m_top, ", ", crop.m_left);
            }
        }
    } else {
        av_dict_set(&opts, "threads", "auto", 0);
    }
    ret = avcodec_open2(tempCodec.get(), decoder, &opts);
    if (ret < 0) {
        logInternal(LogLevel::Error, "Failed opening decoder: ", fileName, ": ", getFfmpegErrorString(ret));
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
        logInternal(LogLevel::Error, "Unknown output pixel format, Manual format conversion must be used: ", fileName);
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
    m_outputHost = outputHost && (decoderContext.get() != nullptr);
    m_formatContext = move(tempFormat);
    m_index = index;
    m_codecContext = move(tempCodec);
    m_seekThreshold = seekThreshold;
    m_noBufferFlush = noBufferFlush && (decoderContext.get() != nullptr);
    m_frameSeekSupported = m_formatContext->iformat->read_seek2 != nullptr;

    // Ensure ping/pong buffers are long enough to handle the maximum number of frames a video may require
    const uint32_t minFrames = std::max(static_cast<uint32_t>(m_seekThreshold), m_bufferLength);

    // Allocate ping and pong buffers
    m_bufferPing.reserve(static_cast<size_t>(minFrames) * 2);
    m_bufferPong.reserve(static_cast<size_t>(minFrames) * 2);

    // Determine actual stream start time
    m_startTimeStamp = getStreamStartTime();
    m_startTimeStamp2 = timeStampToTimeStamp2(m_startTimeStamp);

    // Set stream start time and numbers of frames (done here to ensure correct start timestamp)
    const auto params = getStreamFramesDuration();
    m_totalFrames = params.first;
    m_totalDuration = params.second;

    // Ensure that the stream start at required start time (avoids bogus packets at start of stream)
    av_seek_frame(m_formatContext.get(), m_index, m_startTimeStamp, AVSEEK_FLAG_BACKWARD);

    logInternal(LogLevel::Info, "Stream- Stream created with parameters: bufferLength=", m_bufferLength,
        ", seekThreshold=", m_seekThreshold, ", noBufferFlush=", m_noBufferFlush);
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
    const auto startTime = m_bufferPing.front()->m_frame->best_effort_timestamp;
    if (startTime != 0) {
        // Loop through all current frames and fix the time stamps
        for (auto& i : m_bufferPing) {
            i->m_frame->best_effort_timestamp -= startTime;
            i->m_frame->pts -= startTime;
            i->m_timeStamp = timeStampToTime2(i->m_frame->best_effort_timestamp);
            i->m_frameNum = timeStampToFrame2(i->m_frame->best_effort_timestamp);
        }
        m_lastDecodedTimeStamp = m_bufferPing.back()->m_frame->best_effort_timestamp;
        m_lastValidTimeStamp = m_lastDecodedTimeStamp;
        m_startTimeStamp += timeStamp2ToTimeStamp(startTime);
        m_startTimeStamp2 += startTime;

        logInternal(LogLevel::Warning, "Invalid start time detected: ", timeStampToTime2(startTime));
    }

    // Correct duration/frames for detected start time
    m_totalFrames -= timeStampToFrameNoOffset(m_startTimeStamp);
    m_totalDuration -= timeStampToTimeNoOffset(m_startTimeStamp);

    m_seekThreshold = frameToTimeStamp2(m_seekThreshold == 0 ? getSeekThreshold() : m_seekThreshold);
    logInternal(LogLevel::Info, "initialise - Using final seek threshold: ", m_seekThreshold);
    return true;
}

shared_ptr<Stream> Stream::getStream(const string& fileName, const DecoderOptions& options) noexcept
{
    // Create the device context
    shared_ptr<DecoderContext> deviceContext;
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
            logInternal(LogLevel::Warning, "Cannot get a new frame, End of file has been reached");
            return nullptr;
        }
    }
    // Get frame from ping buffer
    return m_bufferPing.at(m_bufferPingHead);
}

shared_ptr<Frame> Stream::getNextFrame() noexcept
{
    lock_guard<recursive_mutex> lock(m_mutex);
    auto ret = peekNextFrame();
    if (ret != nullptr) {
        LOG_DEBUG("getNextFrame- Retrieved frame: ", ret->getTimeStamp());
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
        // Max number of frames that can be reliably held at any point in time is equal to buffer length
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
            logInternal(LogLevel::Info, "getFrames- Temporarily increasing buffer length: ", m_bufferLength);
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
            LOG_DEBUG("getFramesByIndex- Temporarily increasing buffer length: ", m_bufferLength);
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
        logInternal(LogLevel::Warning, "Trying to seek outside video duration: ", timeStamp);
        return false;
    }
    lock_guard<recursive_mutex> lock(m_mutex);
    LOG_DEBUG("seek- Seek requested: ", timeStamp);
    // Check if we actually have any frames in the current buffer
    if (m_bufferPingHead < m_bufferPing.size()) {
        // Check if the frame is in the current buffer
        const auto halfFrameDuration = frameToTime(1) / 2;
        if ((timeStamp >= m_bufferPing.at(m_bufferPingHead)->getTimeStamp() - halfFrameDuration) &&
            (timeStamp < m_bufferPing.back()->getTimeStamp() + halfFrameDuration)) {
            LOG_DEBUG("seek- Frame found already in buffer: ", timeStamp);
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

            LOG_DEBUG("seek- Using forward decode instead of seek: ", timeStamp);
            // Decode the next block of frames
            return decodeNextBlock(timeStamp2);
        }
    }

    // Seek to desired timestamp
    const auto localTimeStamp = timeToTimeStamp(timeStamp);
    const auto err = avformat_seek_file(m_formatContext.get(), m_index,
        localTimeStamp - timeStamp2ToTimeStamp(m_seekThreshold), localTimeStamp, localTimeStamp, 0);
    if (err < 0) {
        logInternal(LogLevel::Error, "Failed seeking to specified time stamp ", timeStamp, getFfmpegErrorString(err));
        return false;
    }

    // Decode the next block of frames
    return decodeNextBlock(timeStamp2, true);
}

bool Stream::seekFrame(const int64_t frame) noexcept
{
    if (frame >= getTotalFrames()) {
        // Early out if seek is not possible
        logInternal(LogLevel::Warning, "Trying to seek outside video frames: ", frame);
        return false;
    }
    lock_guard<recursive_mutex> lock(m_mutex);
    LOG_DEBUG("seekFrame- Seek requested: ", frame);
    // Check if we actually have any frames in the current buffer
    if (m_bufferPingHead < m_bufferPing.size()) {
        // Check if the frame is in the current buffer
        if ((frame >= m_bufferPing.at(m_bufferPingHead)->getFrameNumber()) &&
            (frame <= m_bufferPing.back()->getFrameNumber())) {
            LOG_DEBUG("seekFrame- Frame found already in buffer: ", frame);
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

            LOG_DEBUG("seekFrame- Using forward decode instead of seek: ", frame);
            // Decode the next block of frames
            return decodeNextBlock(timeStamp2);
        }
    }

    if (!m_frameSeekSupported) {
        logInternal(
            LogLevel::Warning, "Frame seeking is not supported for current file type. Using timebase seek instead");
        return seek(frameToTime(frame));
    }
    // Seek to desired timestamp
    const auto frameInternal = frame + timeStampToFrameNoOffset(m_startTimeStamp);
    const auto err = avformat_seek_file(m_formatContext.get(), m_index,
        frameInternal - timeStampToFrame2(m_seekThreshold), frameInternal, frameInternal, AVSEEK_FLAG_FRAME);
    if (err < 0) {
        logInternal(LogLevel::Error, "Failed to seek to specified frame ", frame, ": ", getFfmpegErrorString(err));
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

int64_t Stream::timeStampToTimeStamp2(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp, m_formatContext->streams[m_index]->time_base, m_codecContext->time_base);
}

int64_t Stream::timeStampToTimeNoOffset(const int64_t timeStamp) const noexcept
{
    return av_rescale_q(timeStamp, m_formatContext->streams[m_index]->time_base, av_make_q(1, AV_TIME_BASE));
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
        if (ret == AVERROR_EOF) {
            eof = true;
            // Send flush packet to decoder
            avcodec_send_packet(m_codecContext.get(), nullptr);
            sentPacket = true;
        } else if (ret < 0) {
            av_packet_unref(&packet);
            logInternal(LogLevel::Error, "Failed to retrieve new packet: ", getFfmpegErrorString(ret));
            return false;
        } else if (!packet.data || !packet.size) {
            // Discard empty packet
            av_packet_unref(&packet);
            LOG_DEBUG("decodeNextBlock- Discarding empty packet");
            continue;
        } else if (m_index == packet.stream_index) {
            const auto packetTimeStamp = getPacketTimeStamp(packet);
            LOG_DEBUG("decodeNextBlock- Received packet: ", packetTimeStamp, ", ", timeStampToTime(packetTimeStamp));
            if (seeking) {
                // Check if a seek went backwards when it should have gone forward
                if (flushTillTime > m_lastDecodedTimeStamp && m_lastPacketTimeStamp > packetTimeStamp) {
                    av_packet_unref(&packet);
                    LOG_DEBUG("decodeNextBlock- Backward seek detected. Skipping packet: ", packetTimeStamp, ", ",
                        timeStampToTime(packetTimeStamp));
                    continue;
                }
                seeking = false;
                if (flushTillTime > m_lastDecodedTimeStamp && m_lastPacketTimeStamp == packetTimeStamp) {
                    // Don't flush buffers as we can just continue to decode from current location
                    LOG_DEBUG("decodeNextBlock- Continuing decode instead of seek: ", packetTimeStamp, ", ",
                        timeStampToTime(packetTimeStamp));
                    continue;
                }
                if (!m_noBufferFlush) {
                    avcodec_flush_buffers(m_codecContext.get());
                    m_lastDecodedTimeStamp = INT64_MIN;
                    LOG_DEBUG("decodeNextBlock- Flushing decode buffers");
                }
                m_lastValidTimeStamp = INT64_MIN;
            }
            m_lastPacketTimeStamp = packetTimeStamp;

            // Convert timebase
            av_packet_rescale_ts(&packet, m_formatContext->streams[m_index]->time_base, m_codecContext->time_base);
            ret = avcodec_send_packet(m_codecContext.get(), &packet);
            while (ret < 0) {
                if (ret == AVERROR_EOF) {
                    LOG_DEBUG("decodeNextBlock- EOF received when sending packet, flushing decoder");
                    avcodec_flush_buffers(m_codecContext.get());
                    ret = avcodec_send_packet(m_codecContext.get(), &packet);
                } else if (ret == AVERROR(EAGAIN)) {
                    LOG_DEBUG("decodeNextBlock- Failed sending packet, EAGAIN");
                    if (!decodeNextFrames(flushTillTime)) {
                        return false;
                    }
                    ret = avcodec_send_packet(m_codecContext.get(), &packet);
                    if (ret == AVERROR(EAGAIN)) {
                        av_packet_unref(&packet);
                        logInternal(LogLevel::Error, "Failed to send packet to decoder: ", getFfmpegErrorString(ret));
                        return false;
                    }
                } else {
                    av_packet_unref(&packet);
                    logInternal(LogLevel::Error, "Failed to send packet to decoder: ", getFfmpegErrorString(ret));
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
    } while ((m_bufferPong.size() < m_bufferLength || flushTillTime >= 0) && !eof);

    if (!processFrames()) {
        return false;
    }

    if (eof) {
        // Check if we got more frames than we should have. This occurs when there are missing frames that are
        // padded in resulting in more output frames than expected.
        while (!m_bufferPong.empty()) {
            if (m_bufferPong.back()->getTimeStamp() < this->getDuration() &&
                m_bufferPong.back()->getTimeStamp() != AV_NOPTS_VALUE) {
                break;
            }
            logInternal(LogLevel::Warning,
                "Additional end frames detected, removing frame: ", m_bufferPong.back()->getTimeStamp());
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
    bool flushAllFrames = false;
    do {
        if (*m_tempFrame == nullptr) {
            m_tempFrame = FramePtr(av_frame_alloc());
            if (*m_tempFrame == nullptr) {
                logInternal(LogLevel::Error, "Failed to allocate new frame");
                return false;
            }
        }
        const auto ret = avcodec_receive_frame(m_codecContext.get(), *m_tempFrame);
        if (ret < 0) {
            if ((ret == AVERROR(EAGAIN)) || (ret == AVERROR_EOF)) {
                LOG_DEBUG("decodeNextFrames- Decoder returned EAGAIN/EOF");
                break;
            }
            logInternal(LogLevel::Error, "Failed to receive decoded frame: ", getFfmpegErrorString(ret));
            return false;
        }

        // Calculate time stamp for frame
        if (m_tempFrame->best_effort_timestamp == AV_NOPTS_VALUE) {
            LOG_DEBUG("decodeNextFrames- Frame did not contain valid timestamp");
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
            LOG_DEBUG("decodeNextFrames- Frame did not contain valid timestamp, corrected timestamp: ",
                m_tempFrame->best_effort_timestamp, ", ", timeStampToTime2(m_tempFrame->best_effort_timestamp));
        }
        int64_t offsetTimeStamp = m_tempFrame->best_effort_timestamp;
        if (offsetTimeStamp == AV_NOPTS_VALUE) {
            // Try and just rebuild it from the previous frame
            offsetTimeStamp = (m_lastDecodedTimeStamp != INT64_MIN) ? m_lastDecodedTimeStamp + frameToTimeStamp2(1) : 0;
            LOG_DEBUG("decodeNextFrames- Frame timestamp still invalid, calculating from last frame: ",
                timeStampToTime2(offsetTimeStamp));
        } else if (m_startTimeStamp2 != 0) {
            // Remove the start time from calculations
            offsetTimeStamp -= m_startTimeStamp2;
            LOG_DEBUG("decodeNextFrames- Frame timestamp corrected for start time: ", offsetTimeStamp, ", ",
                timeStampToTime2(offsetTimeStamp));
        }

        // Store last decoded time stamp
        const auto previousValidTimeStamp = timeStampToTime2(m_lastDecodedTimeStamp);
        m_lastDecodedTimeStamp = offsetTimeStamp;

        if (flushTillTime >= 0) {
            LOG_DEBUG("decodeNextFrames- Flushing till time: ", flushTillTime, ", ", timeStampToTime2(flushTillTime));
            const auto singleFrame = frameToTimeStamp2(1);
            const auto maxDelay = frameToTimeStamp2(getCodecDelay()) * 2;
            const auto doubleTime =
                offsetTimeStamp * 2; // Use double in cases where singleFrame cannot be halved without losses
            const auto doubleFlush = flushTillTime * 2;
            // Keep all frames within +codecDelay of flush time for reorder buffer
            if ((doubleTime + singleFrame) <= doubleFlush || (doubleTime - singleFrame - maxDelay) > doubleFlush) {
                LOG_DEBUG("decodeNextFrames- Dumping frame due to flush time: ", offsetTimeStamp, ", ",
                    timeStampToTime2(offsetTimeStamp));
                // Dump this frame and continue
                av_frame_unref(*m_tempFrame);
                continue;
            }
            if (doubleTime < (doubleFlush + singleFrame) && doubleTime > (doubleFlush - singleFrame)) {
                // We have found the required flush frame
                LOG_DEBUG(
                    "decodeNextFrames- Found flush time: ", offsetTimeStamp, ", ", timeStampToTime2(offsetTimeStamp));
                flushTillTime = INT64_MIN;
            }
            m_lastValidTimeStamp = INT64_MIN;
            flushAllFrames = true;
        }

        // Check if the frame time is below the current one
        if (m_lastValidTimeStamp != INT64_MIN) {
            if (offsetTimeStamp <= m_lastValidTimeStamp) {
                LOG_DEBUG("decodeNextFrames- Dumping frame due to backward time stamp: ", offsetTimeStamp, ", ",
                    timeStampToTime2(offsetTimeStamp));
                // Dump this frame and continue
                av_frame_unref(*m_tempFrame);
                continue;
            }
        }

        auto timeStamp = timeStampToTime2(offsetTimeStamp);
        auto frameNum = timeStampToFrame2(offsetTimeStamp);

        // Update internal timestamps to ensure they are valid
        m_tempFrame->best_effort_timestamp = offsetTimeStamp;
        m_tempFrame->pts = offsetTimeStamp;

        // Check if we have skipped/mis-ordered a frame
        if (previousValidTimeStamp != INT64_MIN && !flushAllFrames) {
            const auto previous = timeToFrame2(previousValidTimeStamp);
            if (frameNum != previous + 1) {
                // Since frames may be received out of order we need to make sure that we receive all frames
                LOG_DEBUG("decodeNextFrames- Invalid frame order detected, will decode until all frames are received");
                flushAllFrames = true;
            }
        }

        // Add the new frame to the pong buffer
        m_bufferPong.emplace_back(
            make_shared<Frame>(m_tempFrame, timeStamp, frameNum, m_formatContext, m_codecContext));
    } while (m_bufferPong.size() < m_bufferLength || flushAllFrames);

    return true;
}

bool Stream::processFrames() noexcept
{
    // Sort the output frames buffer to ensure correct ordering
    stable_sort(m_bufferPong.begin(), m_bufferPong.end(),
        [](const shared_ptr<Frame>& a, const shared_ptr<Frame>& b) { return a->getTimeStamp() < b->getTimeStamp(); });

    auto previousTimeStamp = m_lastValidTimeStamp;
    for (size_t j = 0; j < m_bufferPong.size(); ++j) {
        if (previousTimeStamp != INT64_MIN) {
            // Check for duplicated frames
            const auto previous = timeStampToFrame2(previousTimeStamp);
            if (m_bufferPong[j]->getFrameNumber() == previous) {
                LOG_DEBUG("decodeNextFrames- Deleting duplicated frames: ",
                    m_bufferPong[j]->m_frame->best_effort_timestamp, ", ", m_bufferPong[j]->getTimeStamp());
                if (j != 0) {
                    // Keep the last received of the duplicate frames
                    m_bufferPong.erase(m_bufferPong.begin() + j - 1);
                } else {
                    m_bufferPong.erase(m_bufferPong.begin() + j);
                }
                --j;
                continue;
            }
            previousTimeStamp = m_bufferPong[j]->m_frame->best_effort_timestamp;

            // Check if we have skipped a frame
            if (m_bufferPong[j]->getFrameNumber() != previous + 1) {
                LOG_DEBUG("decodeNextFrames- Skipped frames detected");
                // Fill in missing frames by duplicating the old one
                for (auto i = previous + 1; i < m_bufferPong[j]->getFrameNumber(); i++) {
                    int64_t fillTimeStamp = frameToTime2(i);
                    FramePtr frameClone(av_frame_clone(*m_bufferPong[j]->m_frame));
                    frameClone->best_effort_timestamp = timeToTimeStamp2(fillTimeStamp);
                    frameClone->pts = frameClone->best_effort_timestamp;
                    previousTimeStamp = frameClone->best_effort_timestamp;
                    LOG_DEBUG("decodeNextFrames- Adding missing frame: ", fillTimeStamp);
                    m_bufferPong.insert(m_bufferPong.begin() + j++,
                        make_shared<Frame>(frameClone, fillTimeStamp, i, m_formatContext, m_codecContext));
                }
                --j;
            }
        } else {
            previousTimeStamp = m_bufferPong[j]->m_frame->best_effort_timestamp;
        }
    }

    auto it = m_bufferPong.begin();
    while (it < m_bufferPong.end()) {
        // Perform any required filtering
        if (!processFrame(it->get()->m_frame)) {
            return false;
        }
        if (it->get()->m_frame->height != 0) {
            m_lastValidTimeStamp = it->get()->m_frame->best_effort_timestamp;
            ++it;
        } else {
            LOG_DEBUG("decodeNextFrames- Dropping invalid frame: ", it->get()->m_frame->best_effort_timestamp, ", ",
                it->get()->getTimeStamp());
            m_bufferPong.erase(it);
        }
    }

    return true;
}

bool Stream::processFrame(FramePtr& frame) const noexcept
{
    // Check type of memory pointer requested and perform a memory move
    if (m_outputHost) {
        const auto timeStamp = frame->best_effort_timestamp;
        LOG_DEBUG("processFrame- Copying frame to host: ", frame->best_effort_timestamp, ", ",
            timeStampToTime2(frame->best_effort_timestamp));
        FramePtr frame2(av_frame_alloc());
        if (*frame2 == nullptr) {
            av_frame_unref(*frame);
            logInternal(LogLevel::Error, "Failed to allocate new host frame");
            return false;
        }
        const auto ret2 = av_hwframe_transfer_data(*frame2, *frame, 0);
        av_frame_unref(*frame);
        if (ret2 < 0) {
            av_frame_unref(*frame2);
            logInternal(LogLevel::Error, "Failed to copy frame to host: ", getFfmpegErrorString(ret2));
            return false;
        }
        frame = move(frame2);
        // Ensure proper timestamps after copy
        frame->best_effort_timestamp = timeStamp;
        frame->pts = timeStamp;
    }

    if (m_filterGraph != nullptr) {
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
    return true;
}

void Stream::popFrame() noexcept
{
    if (m_bufferPingHead >= m_bufferPing.size()) {
        logInternal(LogLevel::Error, "No more frames to pop");
        return;
    }
    // Release reference and pop frame
    m_bufferPing.at(m_bufferPingHead++).reset();
}

int32_t Stream::getCodecDelay() const noexcept
{
    return GetCodecDelay(m_codecContext);
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

int32_t Stream::GetCodecDelay(const CodecContextPtr& codec) noexcept
{
    return std::max((codec->codec->capabilities & AV_CODEC_CAP_DELAY ? codec->delay : 0) + codec->has_b_frames, 1);
}

int64_t Stream::getStreamStartTime() const noexcept
{
    // First check if the stream has a start timeStamp
    const AVStream* const stream = m_formatContext->streams[m_index];
    if (stream->start_time != int64_t(AV_NOPTS_VALUE)) {
        return stream->start_time;
    }
    // Seek to the first frame in the video to get information directly from it
    avcodec_flush_buffers(m_codecContext.get());
    if (stream->first_dts != int64_t(AV_NOPTS_VALUE) && stream->codecpar->codec_id != AV_CODEC_ID_HEVC &&
        stream->codecpar->codec_id != AV_CODEC_ID_H264 && stream->codecpar->codec_id != AV_CODEC_ID_MPEG4) {
        return stream->first_dts;
    }
    if (av_seek_frame(m_formatContext.get(), m_index, INT64_MIN, AVSEEK_FLAG_BACKWARD) < 0) {
        logInternal(LogLevel::Error, "Failed to determine stream start time");
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
            const auto pts = (packet.pts != AV_NOPTS_VALUE) ? packet.pts : packet.dts;
            if ((pts != int64_t(AV_NOPTS_VALUE)) &&
                ((pts < startTimeStamp) || (startTimeStamp == int64_t(AV_NOPTS_VALUE)))) {
                startTimeStamp = pts;
            }
            ++i;
        }
        av_packet_unref(&packet);
    }
    // Seek back to start of file so future reads continue back at start
    av_seek_frame(m_formatContext.get(), m_index, INT64_MIN, AVSEEK_FLAG_BACKWARD);
    return (startTimeStamp != int64_t(AV_NOPTS_VALUE)) ? startTimeStamp : 0;
}

std::pair<int64_t, int64_t> Stream::getStreamFramesDuration() const noexcept
{
    const AVStream* const stream = m_formatContext->streams[m_index];
    int64_t frames = INT64_MIN;
    // Check if the number of frames is specified in the stream
    if (stream->nb_frames > 0) {
        frames = stream->nb_frames;
    } else {
        // Attempt to calculate from stream duration, time base and fps
        if (stream->duration > 0) {
            frames = timeStampToFrame(static_cast<int64_t>(stream->duration));
        }
    }

    // First try and get the format duration if specified. For some formats this duration can override the duration
    // specified within each stream which is why it should be checked first.
    int64_t duration = INT64_MIN;
    if (m_formatContext->duration > 0) {
        duration = m_formatContext->duration;
    } else {
        // Check if the duration is specified in the stream
        if (stream->duration > 0) {
            duration = timeStampToTime(stream->duration);
        }
    }

    if (frames == INT64_MIN || duration == INT64_MIN) {
        // If we are at this point then the only option is to scan the entire file and check the DTS/PTS.
        int64_t foundTimeStamp = m_startTimeStamp;

        // Seek last key-frame.
        avcodec_flush_buffers(m_codecContext.get());
        const auto maxSeek = frameToTimeStampNoOffset(1UL << 29UL);
        if (avformat_seek_file(m_formatContext.get(), m_index, INT64_MIN, maxSeek, maxSeek, 0) < 0) {
            logInternal(LogLevel::Error, "Failed to determine number of frames in stream");
            return std::make_pair(frames, duration);
        }

        // Read up to last frame, extending max PTS for every valid PTS value found for the video stream.
        AVPacket packet;
        av_init_packet(&packet);
        while (av_read_frame(m_formatContext.get(), &packet) >= 0) {
            if (packet.stream_index == m_index) {
                const auto found = (packet.pts != AV_NOPTS_VALUE) ? packet.pts : packet.dts;
                if (found > foundTimeStamp) {
                    foundTimeStamp = found;
                }
            }
            av_packet_unref(&packet);
        }

        // Seek back to start of file so future reads continue back at start
        int64_t start = 0LL;
        if (stream->first_dts != int64_t(AV_NOPTS_VALUE)) {
            start = std::min(start, stream->first_dts);
        }
        const int64_t seek = (m_lastPacketTimeStamp != INT64_MIN) ? m_lastPacketTimeStamp : start;
        av_seek_frame(m_formatContext.get(), m_index, seek, AVSEEK_FLAG_BACKWARD);
        if (m_lastPacketTimeStamp != INT64_MIN) {
            // Need to discard packets until correct timestamp is reached
            bool found = false;
            while (!found && av_read_frame(m_formatContext.get(), &packet) >= 0) {
                if (packet.stream_index == m_index) {
                    const auto timestamp = (packet.pts != AV_NOPTS_VALUE) ? packet.pts : packet.dts;
                    if (timestamp == m_lastPacketTimeStamp) {
                        found = true;
                    }
                }
                av_packet_unref(&packet);
            }
        }

        // The detected value is the index of the last frame plus one
        frames = timeStampToFrame(foundTimeStamp) + 1;
        duration = timeStampToTime(foundTimeStamp) + frameToTime(1);
    }

    return std::make_pair(frames, duration);
}
} // namespace Ffr
