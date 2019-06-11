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
#include "FFFrameReader.h"

#include <memory>

struct AVFilterGraph;
struct AVFilterContext;

namespace Ffr {
class Filter
{
    friend class Stream;

    class FilterGraphPtr
    {
        friend class Stream;
        friend class Filter;

        FilterGraphPtr() = default;

        explicit FilterGraphPtr(AVFilterGraph* filterGraph) noexcept;

        [[nodiscard]] AVFilterGraph* get() const noexcept;

        AVFilterGraph* operator->() const noexcept;

        std::shared_ptr<AVFilterGraph> m_filterGraph = nullptr;
    };

public:
    Filter() = delete;

    /**
     * Constructor
     * @param scale         The output resolution or (0, 0) if no scaling should be performed. Scaling is performed
     *  after cropping.
     * @param crop          The output cropping or (0) if no crop should be performed.
     * @param format        The required output pixel format.
     * @param formatContext Context for the format.
     * @param streamIndex   Zero-based index of the stream.
     * @param codecContext  Context for the codec.
     */
    Filter(Resolution scale, Crop crop, PixelFormat format, const Stream::FormatContextPtr& formatContext,
        uint32_t streamIndex, const Stream::CodecContextPtr& codecContext) noexcept;

    ~Filter() = default;

    Filter(const Filter& other) = delete;

    Filter(Filter&& other) noexcept = delete;

    Filter& operator=(const Filter& other) = delete;

    Filter& operator=(Filter&& other) noexcept = delete;

    /**
     * Sends a frame to be filtered
     * @param [in,out] frame The input frame.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool sendFrame(Frame::FramePtr& frame) const noexcept;

    /**
     * Receive frame from filter graph
     * @param [in,out] frame The frame.
     * @returns True if it succeeds, false if it fails.
     */
    [[nodiscard]] bool receiveFrame(Frame::FramePtr& frame) const noexcept;

    /**
     * Gets the width of output frames.
     * @returns The width.
     */
    [[nodiscard]] uint32_t getWidth() const noexcept;

    /**
     * Gets the height of output frames.
     * @returns The height.
     */
    [[nodiscard]] uint32_t getHeight() const noexcept;

    /**
     * Gets the display aspect ratio of output frames.
     * @note This may differ from width/height if stream uses anamorphic pixels.
     * @returns The aspect ratio.
     */
    [[nodiscard]] double getAspectRatio() const noexcept;

    /**
     * Gets pixel format of output frames.
     * @returns The pixel format.
     */
    [[nodiscard]] PixelFormat getPixelFormat() const noexcept;

    /**
     * Gets the frame rate (fps) of the output frames.
     * @note This will not be fully accurate when dealing with VFR video streams.
     * @returns The frame rate in frames per second.
     */
    [[nodiscard]] double getFrameRate() const noexcept;

    /**
     * Gets the storage size of each decoded frame in the output.
     * @returns The frame size in bytes.
     */
    [[nodiscard]] uint32_t getFrameSize() const noexcept;

private:
    FilterGraphPtr m_filterGraph;        /**< The filter graph. */
    AVFilterContext* m_source = nullptr; /**< The input for the filter graph. */
    AVFilterContext* m_sink = nullptr;   /**< The output of the filter graph.*/
};
} // namespace Ffr