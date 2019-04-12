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
#include "FFFRDecoderContext.h"
#include "FFFRStream.h"

#include <map>
#include <memory>
#include <string>
#include <variant>

namespace FfFrameReader {
/**
 * An optional manager class that can be used to store and remember DecoderContext's and Stream's. This can be used
 * to simplify storing and retrieving streams as it removes the need to store the Stream objects in user code.
 * However this does require that a user explicitly remove a used stream once finished with it. This differs from
 * streams created directly from a manual DecoderContext as those objects will automatically destroy themselves once
 * the object goes out of scope. It also differs in that only 1 stream can be created for each file unlike manual
 * DecoderContext objects which can create multiple.
 */
class Manager
{
public:
    Manager();

    ~Manager() = default;

    Manager(const Manager& other) = delete;

    Manager(Manager&& other) noexcept = delete;

    Manager& operator=(const Manager& other) = delete;

    Manager& operator=(Manager&& other) noexcept = delete;

    /**
     * Opens a new stream from a file or retrieves an existing one if already open.
     * @param filename Filename of the file to open.
     * @param type     (Optional) The type of decoding to use. If a stream has already been created then this parameter
     *                 is ignored.
     * @returns The stream if succeeded, false otherwise.
     */
    std::variant<bool, std::shared_ptr<Stream>> getStream(
        const std::string& filename, DecoderContext::DecodeType type = DecoderContext::DecodeType::Software) noexcept;

    /**
     * Releases the stream described by filename
     * @param filename Filename of the file.
     */
    void releaseStream(const std::string& filename) noexcept;

private:
    std::mutex m_mutex;
    std::map<DecoderContext::DecodeType, std::shared_ptr<DecoderContext>> m_decoders;
    std::map<std::string, std::shared_ptr<Stream>> m_streams;
};

/** Values that represent log levels */
enum class LogLevel : int
{
    Quiet = -8,
    Panic = 0,
    Fatal = 8,
    Error = 16,
    Warning = 24,
    Info = 32,
    Verbose = 40,
    Debug = 48,
};

/**
 * Sets log level for all functions within FfFrameReader.
 * @param level The level.
 */
extern void setLogLevel(LogLevel level);
} // namespace FfFrameReader
