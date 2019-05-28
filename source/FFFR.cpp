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
#include "FFFRDecoderContext.h"
#include "FFFRFilter.h"
#include "FFFrameReader.h"

#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/log.h>
#include <libavutil/pixfmt.h>
}

using namespace std;

namespace Ffr {
FfFrameReader::DecoderOptions::DecoderOptions(const DecodeType type) noexcept
    : m_type(type)
{}

bool FfFrameReader::DecoderOptions::operator==(const DecoderOptions& other) const noexcept
{
    const bool ret = this->m_type == other.m_type && this->m_bufferLength == other.m_bufferLength &&
        this->m_device == other.m_device;
    if (!ret) {
        return ret;
    }
    if (this->m_type == DecodeType::Software) {
        return true;
    }
    try {
        if (this->m_type == DecodeType::Cuda) {
            return any_cast<CUcontext>(this->m_context) == any_cast<CUcontext>(other.m_context);
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool FfFrameReader::DecoderOptions::operator!=(const DecoderOptions& other) const noexcept
{
    return !(*this == other);
}

bool FfFrameReader::DecoderOptions::operator<(const DecoderOptions& other) const noexcept
{
    if (this->m_type < other.m_type) {
        return true;
    }
    if (other.m_type < this->m_type) {
        return false;
    }
    if (this->m_bufferLength < other.m_bufferLength) {
        return true;
    }
    if (other.m_bufferLength < this->m_bufferLength) {
        return false;
    }
    try {
        if (this->m_type == DecodeType::Cuda) {
            if (any_cast<CUcontext>(this->m_context) < any_cast<CUcontext>(other.m_context)) {
                return true;
            }
            if (any_cast<CUcontext>(other.m_context) < any_cast<CUcontext>(this->m_context)) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return this->m_device < other.m_device;
}

FfFrameReader::FfFrameReader()
{
    setLogLevel(LogLevel::Error);
}

variant<bool, shared_ptr<Stream>> FfFrameReader::getStream(const string& filename, const DecoderOptions& options) const
    noexcept
{
    // Create the device context
    shared_ptr<DecoderContext> deviceContext = nullptr;
    if (options.m_type != DecodeType::Software) {
        deviceContext = make_shared<DecoderContext>(options.m_type, options.m_context, options.m_device);
        if (deviceContext->m_deviceContext.get() == nullptr) {
            // Device creation failed
            return false;
        }
    }
    const bool outputHost = options.m_outputHost && (options.m_type != DecodeType::Software);
    shared_ptr<Stream> stream = make_shared<Stream>(filename, options.m_bufferLength, deviceContext, outputHost);
    if (stream->m_codecContext.get() == nullptr) {
        // stream creation failed
        return false;
    }

    // Check if any filtering is required
    const bool scaleRequired = (options.m_scale.m_height != 0 || options.m_scale.m_width != 0);
    const bool cropRequired = (options.m_crop.m_top != 0 || options.m_crop.m_bottom != 0 ||
        options.m_crop.m_left != 0 || options.m_crop.m_right != 0);
    const bool formatRequired = (options.m_format != PixelFormat::Auto);

    if (scaleRequired || cropRequired || formatRequired) {
        // Create a new filter object
        const shared_ptr<Filter> filter =
            make_shared<Filter>(options.m_scale, options.m_crop, options.m_format, stream);

        if (filter->m_filterGraph.get() == nullptr) {
            // filter creation failed
            return false;
        }
        stream->setFilter(filter);
    }
    return stream;
}

void FfFrameReader::setLogLevel(const LogLevel level)
{
    av_log_set_level(static_cast<int>(level));
}

void FfFrameReader::log(const std::string& text, const LogLevel level)
{
    av_log(nullptr, static_cast<int>(level), "%s\n", text.c_str());
}
} // namespace Ffr