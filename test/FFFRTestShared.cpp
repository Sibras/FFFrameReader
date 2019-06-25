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
#include "FFFRUtility.h"
#include "FFFrameReader.h"

#include <fstream>
#include <gtest/gtest.h>

extern "C" {
#include <libavutil/imgutils.h>
}

using namespace Ffr;

void saveImage(const PixelFormat format, const uint32_t width, const uint32_t height, const std::string& filename,
    uint8_t* buffer[4], int32_t step[4]) noexcept
{
    if (format != PixelFormat::RGB32FP && format != PixelFormat::RGB8P && format != PixelFormat::RGB8) {
        return;
    }
    std::ofstream ofs;
    try {
        ofs.open(filename + ".ppm", std::ios::binary);
        ASSERT_FALSE(ofs.fail());
        ofs << "P6\n" << width << " " << height << "\n255\n";
        // Loop over each pixel and output to file
        uint32_t offset = 0;
        for (uint32_t i = 0; i < height; ++i) {
            for (uint32_t j = 0; j < width; ++j) {
                uint8_t r, g, b;
                if (format == PixelFormat::RGB32FP) {
                    const auto jOffset = j * sizeof(float);
                    r = static_cast<uint8_t>(*reinterpret_cast<float*>(&(buffer[0][offset + jOffset])) * 255.0f);
                    g = static_cast<uint8_t>(*reinterpret_cast<float*>(&(buffer[1][offset + jOffset])) * 255.0f);
                    b = static_cast<uint8_t>(*reinterpret_cast<float*>(&(buffer[2][offset + jOffset])) * 255.0f);
                } else if (format == PixelFormat::RGB8P) {
                    r = buffer[0][offset + j];
                    g = buffer[1][offset + j];
                    b = buffer[2][offset + j];
                } else {
                    r = buffer[0][offset + (j * 3)];
                    g = buffer[0][offset + (j * 3) + 1];
                    b = buffer[0][offset + (j * 3) + 2];
                }
                ofs << r << g << b;
            }
            offset += step[0];
        }
        ofs.close();
    } catch (...) {
    }
}