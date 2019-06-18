﻿/**
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
#include "FFFRStream.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace Ffr {
class StreamUtils
{
public:
    static AVRational getSampleAspectRatio(const Stream* stream) noexcept;

    static AVPixelFormat getPixelFormat(const Stream* stream) noexcept;

    static AVRational getFrameRate(const Stream* stream) noexcept;

    static AVRational getTimeBase(const Stream* stream) noexcept;

    static void rescale(FramePtr& frame, const AVRational& sourceTimeBase, const AVRational& destTimeBase) noexcept;
};
} // namespace Ffr