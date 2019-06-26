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
#include <cstdint>

__device__ __forceinline__ float clamp(const float f, const float a, const float b)
{
    return fmaxf(a, fminf(f, b));
}

__device__ __forceinline__ float3 YUVToRGB(const uchar3 yuv)
{
    // Get YCbCr values
    const float luma = static_cast<float>(yuv.x);
    const float chromaCb = static_cast<float>(yuv.y) - 128.0f;
    const float chromaCr = static_cast<float>(yuv.z) - 128.0f;

    // Convert to RGB using BT601
    return make_float3(
        luma + 1.13983 * chromaCr, luma - 0.39465f * chromaCb - 0.58060f * chromaCr, luma + 2.03211f * chromaCb);
}

struct Pixel2
{
    float3 m_pixels[2];
};

struct NV12Planes
{
    uint8_t* m_plane1;
    uint8_t* m_plane2;
};

template<typename T>
struct RGBPlanes
{
    T* m_plane1;
    T* m_plane2;
    T* m_plane3;
};

__device__ __forceinline__ Pixel2 getNV12ToRGB(
    const uint32_t x, const uint32_t y, const NV12Planes source, const uint32_t sourceStep)
{
    // NV12 is stored as 2 planes: the first plane contains Y the second plane contains U+V interleaved
    // There are 1 U+V sample for every 2x2 Y block
    //  Y1  Y2  Y3  Y4  Y5  Y5
    //  Y7  Y8  Y9  Y10 Y11 Y12
    //  Y13 Y14 Y15 Y16 Y17 Y18
    //  Y19 Y20 Y21 Y22 Y23 Y24
    //
    //  U1 V1 U2 V2 U2 V3
    //  U4 V4 U5 V5 U6 V6
    //
    //  UV1 is used for Y1 Y2 Y7 Y8
    //  UV2 is used for Y3 Y4 Y9 Y10
    //  UV4 is used for Y13 Y14 Y19 Y20
    // etc.
    // Reading a 2x2 Y block requires 2 memory reads as it is split over 2 rows
    //  To try and be a bit more cache friendly Y is processed in 2 pixels (row) at a time instead of 4
    //  This replaces 2 Y loads at a time with 2 UV loads for each 2xY row

    uchar3 yuvi[2];
    const uint32_t sourceOffset = y * sourceStep + x;
    yuvi[0].x = source.m_plane1[sourceOffset];
    yuvi[1].x = source.m_plane1[sourceOffset + 1];

    const uint32_t chromaOffset = y >> 1;
    const uint32_t chromaSourceOffset = chromaOffset * sourceStep + x;
    const uint8_t chromaCb = source.m_plane2[chromaSourceOffset];
    const uint8_t chromaCr = source.m_plane2[chromaSourceOffset + 1];

    // This doesn't perform any chroma interpolation, this feature would need to be added later if needed

    yuvi[0].y = chromaCb;
    yuvi[0].z = chromaCr;
    yuvi[1].y = chromaCb;
    yuvi[1].z = chromaCr;

    Pixel2 rgb;
    rgb.m_pixels[0] = YUVToRGB(yuvi[0]);
    rgb.m_pixels[1] = YUVToRGB(yuvi[1]);

    return rgb;
}

template<typename T>
class UpPack
{
public:
    typedef float3 Type;
};

template<>
class UpPack<uint8_t>
{
public:
    typedef uchar3 Type;
};

template<typename T>
__device__ __forceinline__ T getRGB(const float3 pixel)
{
    // Normalise float values
    return make_float3(__saturatef(pixel.x / 255.0f), __saturatef(pixel.y / 255.0f), __saturatef(pixel.z / 255.0f));
}

template<>
__device__ __forceinline__ uchar3 getRGB(const float3 pixel)
{
    return make_uchar3(clamp(pixel.x, 0.0f, 255.0f), clamp(pixel.y, 0.0f, 255.0f), clamp(pixel.z, 0.0f, 255.0f));
}

template<typename T>
__device__ __forceinline__ void convertNV12ToRGBP(const NV12Planes source, const uint32_t sourceStep,
    const uint32_t width, const uint32_t height, RGBPlanes<T> dest, const uint32_t destStep)
{
    const uint32_t x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width - 1 || y >= height) {
        return;
    }

    Pixel2 pixels = getNV12ToRGB(x, y, source, sourceStep);

    const auto pixel1 = getRGB<typename UpPack<T>::Type>(pixels.m_pixels[0]);
    const auto pixel2 = getRGB<typename UpPack<T>::Type>(pixels.m_pixels[1]);
    const uint32_t destOffset = y * destStep + x;
    dest.m_plane1[destOffset] = pixel1.x;
    dest.m_plane1[destOffset + 1] = pixel2.x;
    dest.m_plane2[destOffset] = pixel1.y;
    dest.m_plane2[destOffset + 1] = pixel2.y;
    dest.m_plane3[destOffset] = pixel1.z;
    dest.m_plane3[destOffset + 1] = pixel2.z;
}

extern "C" {
__global__ void convertNV12ToRGB8P(const NV12Planes source, const uint32_t sourceStep, const uint32_t width,
    const uint32_t height, const RGBPlanes<uint8_t> dest, const uint32_t destStep)
{
    convertNV12ToRGBP<uint8_t>(source, sourceStep, width, height, dest, destStep);
}

__global__ void convertNV12ToRGB32FP(const NV12Planes source, const uint32_t sourceStep, const uint32_t width,
    const uint32_t height, const RGBPlanes<float> dest, const uint32_t destStep)
{
    convertNV12ToRGBP<float>(source, sourceStep, width, height, dest, destStep / sizeof(float));
}
}