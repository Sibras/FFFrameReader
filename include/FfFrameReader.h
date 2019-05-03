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

/**
 * Example:
 * To use this library you first need to create a decoder context that is used to represent the decoding options that
 * are to be used when decoding files. A default DecoderContext object will use normal software decoding on the CPU and
 * can be created using the following:
 *
 * DecoderContext context;
 *
 * Decoder contexts can also be optionally passed a set of options (@see DecoderOptions) that can be used to enable
 * hardware accelerated decoding and various other decoding parameters. For instance to use NVIDIA GPU accelerated
 * decoding you can create a decoder context as follows:
 *
 * DecoderContext context(DecoderContext::DecodeType::Nvdec);
 *
 * Once a decoder has been created it can then be used to open various files such as:
 *
 * auto ret = context.getStream(fileName);
 * if (ret.index() == 0) {
 *     // File opening has failed
 * }
 * auto stream = std::get<1>(ret);
 *
 * A stream object can then be used to get information about the opened file (such as resolution, duration etc.) and to
 * read image frames from the video. To get the next frame in a video you can use the following in a loop:
 *
 * auto ret2 = stream->getNextFrame();
 * if (ret2.index() == 0) {
 *     // Failed to get the next frame, there was either an internal error or the end of the file has been reached
 * }
 * auto frame = std::get<1>(ret2);
 *
 * This creates a frame object that has the information for the requested frame (such as its time stamp, frame number
 * etc.). It also has the actual image data itself that can be retrieved as follows:
 *
 * uint8_t* const* data = frame->getFrameData();
 *
 * The format of the data stored in the returned pointer depends on the input video and the decoder options. By default
 * many videos will be coded using YUV422 which in that case means that data is a pointer to an array of 3 memory
 * pointers, where each of the 3 pointers points to either the Y,U or V planes respectively.
 *
 * By default when using hardware decoding the decoded frames will be output in the default memory format of the chosen
 * decoder. So for instance when using NVDec the output memory will be a CUDA GPU memory pointer.
 *
 * In addition to just reading frames in sequence from start to finish, a stream object also supports seeking. Seeks can
 * be performed on either duration time stamps of specific frame numbers as follows:
 *
 * if (!stream->seek(2000)) {
 *     // Failed to seek to requested time stamp
 * }
 *
 */

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
     * @param options  (Optional) Options for controlling decoding.
     * @returns The stream if succeeded, false otherwise.
     */
    std::variant<bool, std::shared_ptr<Stream>> getStream(
        const std::string& filename, const DecoderContext::DecoderOptions& options = {}) noexcept;

    /**
     * Releases the stream described by filename
     * @param filename Filename of the file.
     */
    void releaseStream(const std::string& filename) noexcept;

private:
    std::mutex m_mutex;
    std::map<DecoderContext::DecoderOptions, std::shared_ptr<DecoderContext>> m_decoders;
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
