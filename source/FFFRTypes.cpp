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
#include "FFFRTypes.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_cuda.h>
}

using namespace std;

namespace Ffr {
DecoderOptions::DecoderOptions(const DecodeType type) noexcept
    : m_type(type)
{}

bool DecoderOptions::operator==(const DecoderOptions& other) const noexcept
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

bool DecoderOptions::operator!=(const DecoderOptions& other) const noexcept
{
    return !(*this == other);
}

bool DecoderOptions::operator<(const DecoderOptions& other) const noexcept
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

FormatContextPtr::FormatContextPtr(AVFormatContext* formatContext) noexcept
    : m_formatContext(formatContext, [](AVFormatContext* p) { avformat_close_input(&p); })
{}

AVFormatContext* FormatContextPtr::operator->() const noexcept
{
    return m_formatContext.get();
}

AVFormatContext* FormatContextPtr::get() const noexcept
{
    return m_formatContext.get();
}

CodecContextPtr::CodecContextPtr(AVCodecContext* codecContext) noexcept
    : m_codecContext(codecContext, [](AVCodecContext* p) { avcodec_free_context(&p); })
{}

AVCodecContext* CodecContextPtr::operator->() const noexcept
{
    return m_codecContext.get();
}

AVCodecContext* CodecContextPtr::get() const noexcept
{
    return m_codecContext.get();
}

FramePtr::FramePtr(AVFrame* frame) noexcept
    : m_frame(frame)
{}

FramePtr::~FramePtr() noexcept
{
    if (m_frame != nullptr) {
        av_frame_free(&m_frame);
    }
}

FramePtr::FramePtr(FramePtr&& other) noexcept
    : m_frame(other.m_frame)
{
    other.m_frame = nullptr;
}

FramePtr& FramePtr::operator=(FramePtr& other) noexcept
{
    m_frame = other.m_frame;
    other.m_frame = nullptr;
    return *this;
}

FramePtr& FramePtr::operator=(FramePtr&& other) noexcept
{
    m_frame = other.m_frame;
    other.m_frame = nullptr;
    return *this;
}

AVFrame*& FramePtr::operator*() noexcept
{
    return m_frame;
}

const AVFrame* FramePtr::operator*() const noexcept
{
    return m_frame;
}

AVFrame*& FramePtr::operator->() noexcept
{
    return m_frame;
}

const AVFrame* FramePtr::operator->() const noexcept
{
    return m_frame;
}

OutputFormatContextPtr::OutputFormatContextPtr(AVFormatContext* formatContext) noexcept
    : m_formatContext(formatContext, [](AVFormatContext* p) { avformat_free_context(p); })
{}

AVFormatContext* OutputFormatContextPtr::get() const noexcept
{
    return m_formatContext.get();
}

AVFormatContext* OutputFormatContextPtr::operator->() const noexcept
{
    return m_formatContext.get();
}
} // namespace Ffr