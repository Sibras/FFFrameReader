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
#include "FfFrameReader.h"

extern "C" {
#include <libavutil/log.h>
}
using namespace std;

namespace FfFrameReader {
Manager::Manager()
{
    setLogLevel(LogLevel::Error);
}

variant<bool, shared_ptr<Stream>> Manager::getStream(
    const string& filename, const DecoderContext::DecodeType type) noexcept
{
    try {
        lock_guard<mutex> lock(m_mutex);
        // Check if file already open
        const auto foundFile = m_streams.find(filename);
        shared_ptr<DecoderContext> found;
        if (foundFile == m_streams.end()) {
            // Check if a manager already registered for type
            const auto foundManager = m_decoders.find(type);
            if (foundManager == m_decoders.end()) {
                m_decoders.emplace(type, make_shared<DecoderContext>(type));
                found = m_decoders.find(type)->second;
            } else {
                found = foundManager->second;
            }
        } else {
            return foundFile->second;
        }

        // Create a new stream using the requested manager
        const auto newStream = found->getStream(filename);
        if (newStream.index() != 0) {
            if (foundFile == m_streams.end()) {
                m_streams.emplace(filename, get<1>(newStream));
            }
            return get<1>(newStream);
        }
    } catch (...) {
    }
    return false;
}

void Manager::releaseStream(const string& filename) noexcept
{
    try {
        lock_guard<mutex> lock(m_mutex);
        const auto found = m_streams.find(filename);
        if (found != m_streams.end()) {
            m_streams.erase(found);
        }
    } catch (...) {
    }
}

void setLogLevel(const LogLevel level)
{
    av_log_set_level(static_cast<int>(level));
}
} // namespace FfFrameReader