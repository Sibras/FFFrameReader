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
#include "FFFRFrame.h"

#include "FFFRUtility.h"

#include <algorithm>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

using namespace std;

namespace Ffr {
Frame::Frame(FramePtr& frame, const int64_t timeStamp, const int64_t frameNum, FormatContextPtr formatContext,
    CodecContextPtr codecContext) noexcept
    : m_frame(move(frame))
    , m_timeStamp(timeStamp)
    , m_frameNum(frameNum)
    , m_formatContext(move(formatContext))
    , m_codecContext(move(codecContext))
{}

int64_t Frame::getTimeStamp() const noexcept
{
    return m_timeStamp;
}

int64_t Frame::getFrameNumber() const noexcept
{
    return m_frameNum;
}

std::pair<uint8_t* const, int32_t> Frame::getFrameData(const uint32_t plane) const noexcept
{
    int32_t lineSize = m_frame->linesize[plane];
    uint8_t* const data = m_frame->data[plane];
    return make_pair(data, lineSize);
}

uint32_t Frame::getWidth() const noexcept
{
    return m_frame->width;
}

uint32_t Frame::getHeight() const noexcept
{
    return m_frame->height;
}

double Frame::getAspectRatio() const noexcept
{
    // TODO: Handle this with anamorphic
    return static_cast<double>(getWidth()) / static_cast<double>(getHeight());
}

PixelFormat Frame::getPixelFormat() const noexcept
{
    if (m_frame->hw_frames_ctx == nullptr) {
        return Ffr::getPixelFormat(static_cast<AVPixelFormat>(m_frame->format));
    }
    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(m_frame->hw_frames_ctx->data);
    return Ffr::getPixelFormat(framesContext->sw_format);
}

int32_t Frame::getNumberFrames() const noexcept
{
    if (m_frame->hw_frames_ctx == nullptr) {
        return av_pix_fmt_count_planes(static_cast<AVPixelFormat>(m_frame->format));
    }
    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(m_frame->hw_frames_ctx->data);
    return av_pix_fmt_count_planes(framesContext->sw_format);
}

DecodeType Frame::getDataType() const noexcept
{
    if (m_frame->format == AV_PIX_FMT_CUDA) {
        return DecodeType::Cuda;
    }
    return DecodeType::Software;
}
} // namespace Ffr