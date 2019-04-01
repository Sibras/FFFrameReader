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
#include "FFFRLog.h"
#include "FFFRManager.h"
#include "FfFrameReader.h"

#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>

extern "C" {
#include <libavutil/log.h>
}

using namespace std;

void logCallback(void*, const int level, const char* format, const va_list vl)
{
    if (level > av_log_get_level()) {
        return;
    }
    char pTempChar[1024];
    if (vsnprintf(pTempChar, 1024 * sizeof(char), format, vl) > 0) {
        if (level >= AV_LOG_ERROR) {
            FfFrameReader::Interface::logError(pTempChar);
        } else if (level >= AV_LOG_WARNING) {
            FfFrameReader::Interface::logWarning(pTempChar);
        } else {
            FfFrameReader::Interface::logInfo(pTempChar);
        }
    }
}

namespace FfFrameReader {
class Module
{
    friend class Interface;

public:
    Module() noexcept
    {
        // Setup FFmpeg logging
        av_log_set_level(AV_LOG_WARNING);
        av_log_set_callback(logCallback);
    }

    ~Module() noexcept = default;

    Module(const Module& other) = delete;

    Module(Module&& other) noexcept = delete;

    Module& operator=(const Module& other) = delete;

    Module& operator=(Module&& other) noexcept = delete;

protected:
    mutex m_mutex;
    map<Interface::DecoderType, shared_ptr<Manager>> m_managers;
    map<string, shared_ptr<Manager>> m_filenames;
    Log m_log;
};

Module g_module;

FrameInterface::FrameInterface(shared_ptr<Frame> frame) noexcept
    : m_frame(move(frame))
{}

int64_t FrameInterface::getTimeStamp() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", 0);

    // Call the member function
    return m_frame->getTimeStamp();
}

int64_t FrameInterface::getFrameNumber() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", 0);

    // Call the member function
    return m_frame->getFrameNumber();
}

uint8_t* FrameInterface::getFrameData() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", nullptr);

    // Call the member function
    return m_frame->getFrameData();
}

uint32_t FrameInterface::getWidth() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", 0);

    // Call the member function
    return m_frame->getWidth();
}

uint32_t FrameInterface::getHeight() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", 0);

    // Call the member function
    return m_frame->getHeight();
}

double FrameInterface::getAspectRatio() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_frame.get() != nullptr, "Frame operation requested on null frame", 0.0);

    // Call the member function
    return m_frame->getAspectRatio();
}

uint32_t StreamInterface::getWidth() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    return m_stream->getWidth();
}

uint32_t StreamInterface::getHeight() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    return m_stream->getHeight();
}

double StreamInterface::getAspectRatio() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    return m_stream->getAspectRatio();
}

int64_t StreamInterface::getTotalFrames() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    return m_stream->getTotalFrames();
}

int64_t StreamInterface::getDuration() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    return m_stream->getDuration();
}

variant<bool, FrameInterface> StreamInterface::getNextFrame() const noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    const auto frame = m_stream->getNextFrame();
    try {
        if (frame.index() != 0) {
            return FrameInterface(get<1>(frame));
        }
    } catch (...) {
    }
    return false;
}

variant<bool, vector<FrameInterface>> StreamInterface::getNextFrameSequence(const vector<uint64_t>& frameSequence) const
    noexcept
{
    // Check if initialised to a valid stream
    FFFRASSERT(m_stream.get() != nullptr, "Stream operation requested on null stream", 0);

    // Call the member function
    const auto frames = m_stream->getNextFrameSequence(frameSequence);
    try {
        if (frames.index() != 0) {
            vector<FrameInterface> newFrames;
            for (const auto& i : get<1>(frames)) {
                newFrames.emplace_back(i);
            }
            return newFrames;
        }
    } catch (...) {
    }
    return false;
}

StreamInterface::StreamInterface(shared_ptr<Stream> stream) noexcept
    : m_stream(move(stream))
{}

variant<bool, StreamInterface> Interface::getStream(const string& filename, const DecoderType type) noexcept
{
    try {
        lock_guard<mutex> lock(g_module.m_mutex);
        // Check if file already open
        const auto foundFile = g_module.m_filenames.find(filename);
        shared_ptr<Manager> found;
        if (foundFile == g_module.m_filenames.end()) {
            // Check if a manager already registered for type
            const auto foundManager = g_module.m_managers.find(type);
            if (foundManager == g_module.m_managers.end()) {
                Manager::DecodeType decodeType = Manager::DecodeType::Software;
                if (type == DecoderType::Nvdec) {
                    decodeType = Manager::DecodeType::Nvdec;
                } else if (type != DecoderType::Software) {
                    FFFRASSERT(false, "Decoder type conversion not implemeneted", false);
                }
                g_module.m_managers.emplace(type, make_shared<Manager>(decodeType));
                found = g_module.m_managers.find(type)->second;
            } else {
                found = foundManager->second;
            }
        } else {
            found = foundFile->second;
        }

        // Create a new stream using the requested manager
        const auto newStream = found->getStream(filename);
        if (newStream.index() != 0) {
            if (foundFile == g_module.m_filenames.end()) {
                g_module.m_filenames.emplace(filename, found);
            }
            return StreamInterface(get<1>(newStream));
        }
    } catch (...) {
    }
    return false;
}

void Interface::releaseStream(const string& filename) noexcept
{
    try {
        lock_guard<mutex> lock(g_module.m_mutex);
        const auto found = g_module.m_filenames.find(filename);
        if (found != g_module.m_filenames.end()) {
            found->second->releaseStream(filename);
            g_module.m_filenames.erase(found);
        }
    } catch (...) {
    }
}

void Interface::logMessage(const string& text, LogLevel severity) noexcept
{
    g_module.m_log.logMessage(text, static_cast<Log::LogLevel>(severity));
}

void Interface::logInfo(const string& text) noexcept
{
    g_module.m_log.logInfo(text);
}

void Interface::logWarning(const string& text) noexcept
{
    g_module.m_log.logWarning(text);
}

void Interface::logError(const string& text) noexcept
{
    g_module.m_log.logError(text);
}

void Interface::logStackTrace() noexcept
{
    g_module.m_log.logStackTrace();
}
} // namespace FfFrameReader