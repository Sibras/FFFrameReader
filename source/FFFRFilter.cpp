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
#include "FFFRFilter.h"

#include "FFFRUtility.h"

#include <string>
using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace Ffr {
Filter::FilterGraphPtr::FilterGraphPtr(AVFilterGraph* filterGraph) noexcept
    : m_filterGraph(filterGraph, [](AVFilterGraph* p) { avfilter_graph_free(&p); })
{}

AVFilterGraph* Filter::FilterGraphPtr::get() const noexcept
{
    return m_filterGraph.get();
}

AVFilterGraph* Filter::FilterGraphPtr::operator->() const noexcept
{
    return m_filterGraph.get();
}

Filter::Filter(const Resolution scale, const Crop crop, PixelFormat format, const FormatContextPtr& formatContext,
    const uint32_t streamIndex, const CodecContextPtr& codecContext) noexcept
{
    // Make a filter graph to perform any required conversions
    FilterGraphPtr tempGraph(avfilter_graph_alloc());
    const auto bufferIn = avfilter_get_by_name("buffer");
    const auto bufferOut = avfilter_get_by_name("buffersink");

    if (tempGraph.get() == nullptr || bufferIn == nullptr || bufferOut == nullptr) {
        log("Unable to create filter graph"s, LogLevel::Error);
        return;
    }

    // Create the input and output buffers
    const auto bufferInContext = avfilter_graph_alloc_filter(tempGraph.get(), bufferIn, "src");
    const auto bufferOutContext = avfilter_graph_alloc_filter(tempGraph.get(), bufferOut, "sink");
    if (bufferInContext == nullptr || bufferOutContext == nullptr) {
        log("Could not allocate the filter buffer instance"s, LogLevel::Error);
        return;
    }

    // Set the input buffer parameters
    const auto inFormat = codecContext->pix_fmt == AV_PIX_FMT_NONE ?
        static_cast<AVPixelFormat>(formatContext->streams[streamIndex]->codecpar->format) :
        codecContext->pix_fmt;
    const auto inHeight = codecContext->height;
    const auto inWidth = codecContext->width;
    auto inParams = av_buffersrc_parameters_alloc();
    inParams->format = inFormat;
    inParams->frame_rate = codecContext->framerate;
    inParams->height = inHeight;
    inParams->width = inWidth;
    inParams->sample_aspect_ratio = codecContext->sample_aspect_ratio;
    inParams->time_base = av_inv_q(inParams->frame_rate);
    if (codecContext->hw_frames_ctx != nullptr) {
        inParams->hw_frames_ctx = av_buffer_ref(codecContext->hw_frames_ctx);
    }
    auto ret = av_buffersrc_parameters_set(bufferInContext, inParams);
    if (ret < 0) {
        av_free(inParams);
        log("Failed setting filter input parameters: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }
    av_free(inParams);
    ret = avfilter_init_str(bufferInContext, nullptr);
    if (ret < 0) {
        log("Could not initialize the filter input instance: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }

    // Determine which settings require a filter stage
    const bool cropRequired = (crop.m_top != 0 || crop.m_bottom != 0 || crop.m_left != 0 || crop.m_right != 0);
    const bool scaleRequired = (scale.m_height != 0 || scale.m_width != 0);
    const bool formatRequired =
        (format != PixelFormat::Auto && format != Ffr::getPixelFormat(static_cast<AVPixelFormat>(inFormat)));

    // Set the output buffer parameters
    if (formatRequired) {
        enum AVPixelFormat pixelFormats[] = {static_cast<AVPixelFormat>(format)};
        ret = av_opt_set_bin(bufferOutContext, "pix_fmts", reinterpret_cast<const uint8_t*>(pixelFormats),
            sizeof(pixelFormats), AV_OPT_SEARCH_CHILDREN);
        ret = (ret < 0) ? ret : avfilter_init_str(bufferOutContext, nullptr);
    } else {
        ret = avfilter_init_str(bufferOutContext, nullptr);
    }
    if (ret < 0) {
        log("Could not initialize the filter output instance: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }

    AVFilterContext* nextFilter = bufferInContext;
    if (codecContext->hwaccel == nullptr) {
        if (cropRequired) {
            const auto cropFilter = avfilter_get_by_name("crop");
            if (cropFilter == nullptr) {
                log("Unable to create crop filter"s, LogLevel::Error);
                return;
            }
            const auto cropContext = avfilter_graph_alloc_filter(tempGraph.get(), cropFilter, "crop");
            if (cropContext == nullptr) {
                log("Unable to create crop filter context"s, LogLevel::Error);
                return;
            }
            if (crop.m_top != 0 || crop.m_bottom != 0) {
                const uint32_t height = inHeight - crop.m_top - crop.m_bottom;
                try {
                    av_opt_set(cropContext, "h", to_string(height).c_str(), AV_OPT_SEARCH_CHILDREN);
                    av_opt_set(cropContext, "y", to_string(crop.m_top).c_str(), AV_OPT_SEARCH_CHILDREN);
                } catch (...) {
                    return;
                }
            }
            if (crop.m_left != 0 || crop.m_right != 0) {
                const uint32_t width = inWidth - crop.m_left - crop.m_right;
                try {
                    av_opt_set(cropContext, "w", to_string(width).c_str(), AV_OPT_SEARCH_CHILDREN);
                    av_opt_set(cropContext, "x", to_string(crop.m_left).c_str(), AV_OPT_SEARCH_CHILDREN);
                } catch (...) {
                    return;
                }
            }
            // Link the filter into chain
            ret = avfilter_link(nextFilter, 0, cropContext, 0);
            if (ret < 0) {
                log("Unable to link crop filter"s, LogLevel::Error);
                return;
            }
            nextFilter = cropContext;
        }
        if (scaleRequired || formatRequired) {
            const auto scaleFilter = avfilter_get_by_name("scale");
            if (scaleFilter == nullptr) {
                log("Unable to create scale filter"s, LogLevel::Error);
                return;
            }
            const auto scaleContext = avfilter_graph_alloc_filter(tempGraph.get(), scaleFilter, "scale");
            if (scaleContext == nullptr) {
                log("Unable to create scale filter context"s, LogLevel::Error);
                return;
            }

            try {
                av_opt_set(scaleContext, "w", to_string(scale.m_width != 0 ? scale.m_width : inWidth).c_str(),
                    AV_OPT_SEARCH_CHILDREN);
                av_opt_set(scaleContext, "h", to_string(scale.m_height != 0 ? scale.m_height : inHeight).c_str(),
                    AV_OPT_SEARCH_CHILDREN);
            } catch (...) {
                return;
            }
            // av_opt_set(scaleContext, "out_color_matrix", "bt709", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(scaleContext, "out_range", "full", AV_OPT_SEARCH_CHILDREN);

            // Link the filter into chain
            ret = avfilter_link(nextFilter, 0, scaleContext, 0);
            if (ret < 0) {
                log("Unable to link scale filter"s, LogLevel::Error);
                return;
            }
            nextFilter = scaleContext;
        }
    } else {
        auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(codecContext->hw_device_ctx->data);
        if (deviceContext->type == AV_HWDEVICE_TYPE_CUDA) {
            // Scale and crop are performed by decoder
            if (formatRequired) {
                // TODO: Needs additions to ffmpegs filters for cuda accelerated format conversion
                log("Feature not yet implemented for selected decoding type"s, LogLevel::Error);
                return;
            }
        } else {
            log("Feature not yet implemented for selected decoding type"s, LogLevel::Error);
            return;
        }
    }

    // Link final filter sequence
    ret = avfilter_link(nextFilter, 0, bufferOutContext, 0);
    if (ret < 0) {
        log("Could not set the filter links: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }

    // Configure the completed graph
    ret = avfilter_graph_config(tempGraph.get(), nullptr);
    if (ret < 0) {
        log("Failed configuring filter graph: "s += getFfmpegErrorString(ret), LogLevel::Error);
        return;
    }

    // Make a new filter
    m_filterGraph = move(tempGraph);
    m_source = bufferInContext;
    m_sink = bufferOutContext;
}

bool Filter::sendFrame(FramePtr& frame) const noexcept
{
    const auto err = av_buffersrc_add_frame(m_source, *frame);
    if (err < 0) {
        log("Failed to submit frame to filter graph: "s += getFfmpegErrorString(err), LogLevel::Error);
        return false;
    }
    return true;
}

bool Filter::receiveFrame(FramePtr& frame) const noexcept
{
    // Get the next available frame
    const auto err = av_buffersink_get_frame(m_sink, *frame);
    if (err < 0) {
        if ((err == AVERROR(EAGAIN)) || (err == AVERROR_EOF)) {
            return true;
        }
        log("Failed to receive frame from filter graph: "s += getFfmpegErrorString(err), LogLevel::Error);
        return false;
    }
    return true;
}

uint32_t Filter::getWidth() const noexcept
{
    return av_buffersink_get_w(m_sink);
}

uint32_t Filter::getHeight() const noexcept
{
    return av_buffersink_get_h(m_sink);
}

double Filter::getAspectRatio() const noexcept
{
    const auto ar = av_buffersink_get_sample_aspect_ratio(m_sink);
    if (ar.num) {
        return static_cast<double>(getWidth()) / static_cast<double>(getHeight()) * av_q2d(ar);
    }
    return static_cast<double>(getWidth()) / static_cast<double>(getHeight());
}

PixelFormat Filter::getPixelFormat() const noexcept
{
    return Ffr::getPixelFormat(static_cast<AVPixelFormat>(av_buffersink_get_format(m_sink)));
}

double Filter::getFrameRate() const noexcept
{
    return av_q2d(av_buffersink_get_frame_rate(m_sink));
}

uint32_t Filter::getFrameSize() const noexcept
{
    return av_image_get_buffer_size(
        static_cast<AVPixelFormat>(av_buffersink_get_format(m_sink)), getWidth(), getHeight(), 32);
}
} // namespace Ffr