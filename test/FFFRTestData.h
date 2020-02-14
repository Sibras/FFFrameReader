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
#include "FFFRTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#define FFFR_INTERNAL_FILES 1

struct TestParams
{
    std::string m_fileName;
    int64_t m_width;
    int64_t m_height;
    double m_aspectRatio;
    int64_t m_totalFrames;
    int64_t m_duration;
    double m_frameRate;
    int64_t m_frameTime;
    Ffr::PixelFormat m_format;
};

// The following test files are made available as part of blender.org open movie projects.
// To run tests you must first download the files from the corresponding web location
static std::vector<TestParams> g_testData = {
    {"data/bbb_sunflower_1080p_30fps_normal.mp4", 1920, 1080, 16.0 / 9.0, 19034, 634466666, 30.0, 33333,
        Ffr::PixelFormat::YUV420P},
    {"data/board_game-h264.mkv", 1280, 720, 16.0 / 9.0, 50, 2000000, 25.0, 40000, Ffr::PixelFormat::YUV420P},
    {"data/tvl_missing_pts.avi", 2048, 2048, 1.0, 373, 14920000, 25.0, 40000, Ffr::PixelFormat::YUV420P},
    {"data/board_game-h264-cropped.mkv", 52, 28, 52.0 / 28.0, 50, 2000000, 25.0, 40000, Ffr::PixelFormat::YUV420P},
#if FFFR_INTERNAL_FILES
    // These are internal testing files and are not redistributed
    {"data/CLIP0000818_000_77aabf.mkv", 3840, 2160, 16.0 / 9.0, 1116, 44640000, 25.0, 40000, Ffr::PixelFormat::YUV420P},
    {"data/MenMC_50BR_Finals_Canon.MP4", 3840, 2160, 16.0 / 9.0, 2124, 84960000, 25.0, 40000,
        Ffr::PixelFormat::YUV420P},
    {"data/MVI_0048.MP4", 3840, 2160, 16.0 / 9.0, 8592, 171840000, 50.0, 20000, Ffr::PixelFormat::YUV420P},
    {"data/Women_400IM_Heat2_Sony.MXF", 3840, 2160, 16.0 / 9.0, 9204, 368160000, 25.0, 40000,
        Ffr::PixelFormat::YUV420P},
    {"data/20191007a-calib-left.mkv", 3840, 2160, 16.0 / 9.0, 2788, 55760000, 50.0, 20000, Ffr::PixelFormat::YUV420P},
    {"data/M100FS_Final_WorldChampionshipTrials2019.mp4", 3840, 2160, 16.0 / 9.0, 3960, 79200000, 50.0, 20000,
        Ffr::PixelFormat::YUV420P},
#endif
};
