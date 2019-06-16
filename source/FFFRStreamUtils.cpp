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
#include "FFFRStreamUtils.h"

#include "FFFRFilter.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
}

namespace Ffr {
AVRational StreamUtils::getSampleAspectRatio(const Stream* const stream) noexcept
{
    return stream->m_filterGraph == nullptr ? stream->m_codecContext->sample_aspect_ratio :
                                              av_buffersink_get_sample_aspect_ratio(stream->m_filterGraph->m_sink);
}

AVPixelFormat StreamUtils::getPixelFormat(const Stream* const stream) noexcept
{
    AVPixelFormat ret;
    if (stream->m_filterGraph.get() != nullptr) {
        ret = static_cast<AVPixelFormat>(av_buffersink_get_format(stream->m_filterGraph->m_sink));
    }
    ret = stream->m_codecContext->sw_pix_fmt;
    // Remove old deprecated jpeg formats
    if (ret == AV_PIX_FMT_YUVJ411P) {
        return AV_PIX_FMT_YUV411P;
    }
    if (ret == AV_PIX_FMT_YUVJ420P) {
        return AV_PIX_FMT_YUV420P;
    }
    if (ret == AV_PIX_FMT_YUVJ422P) {
        return AV_PIX_FMT_YUV422P;
    }
    if (ret == AV_PIX_FMT_YUVJ440P) {
        return AV_PIX_FMT_YUV440P;
    }
    if (ret == AV_PIX_FMT_YUVJ444P) {
        return AV_PIX_FMT_YUV444P;
    }
    return ret;
}

AVRational StreamUtils::getFrameRate(const Stream* stream) noexcept
{
    if (stream->m_filterGraph.get() != nullptr) {
        return av_buffersink_get_frame_rate(stream->m_filterGraph->m_sink);
    }
    if (stream->m_codecContext->framerate.num != 0) {
        return stream->m_codecContext->framerate;
    }
    // Should never get here
    return stream->m_formatContext->streams[stream->m_index]->r_frame_rate;
}

AVRational StreamUtils::getTimeBase(const Stream* stream) noexcept
{
    return stream->m_filterGraph == nullptr ? stream->m_codecContext->time_base :
                                              av_buffersink_get_time_base(stream->m_filterGraph->m_sink);
}

void StreamUtils::rescale(FramePtr& frame, const AVRational& sourceTimeBase, const AVRational& destTimeBase) noexcept
{
    frame->pts = av_rescale_q(frame->pts, sourceTimeBase, destTimeBase);
}
} // namespace Ffr