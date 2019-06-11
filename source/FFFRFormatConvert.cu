#include <cstdint>
#include <cuda_runtime.h>

namespace Ffr {
__device__ __forceinline__ float clamp(const float f, const float a, const float b)
{
    return fmaxf(a, fminf(f, b));
}

__device__ __forceinline__ float3 YUVToRGB(const char3 yuv)
{
    // Get YCbCr values
    const float luma = static_cast<float>(yuv.x);
    const float chromaCb = static_cast<float>(yuv.y) - 128.0f;
    const float chromaCr = static_cast<float>(yuv.z) - 128.0f;

    // Convert to RGB using BT601
    return make_float3(clamp(luma + 1.13983 * chromaCr, 0, 255.0f),
        clamp(luma - 0.39465f * chromaCb - 0.58060f * chromaCr, 0, 255.0f),
        clamp(luma + 2.03211f * chromaCb, 0, 255.0f));
}

struct Pixel2
{
    float3 m_pixels[2];
};

__device__ __forceinline__ Pixel2 getNV12ToRGB(
    const uint32_t x, const uint32_t y, const uint8_t* const source[2], const uint32_t sourceStep)
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

    char3 yuvi[2];
    const uint32_t sourceOffset = y * sourceStep + x;
    yuvi[0].x = source[0][sourceOffset];
    yuvi[1].x = source[0][sourceOffset + 1];

    const uint32_t chromaOffset = y >> 1;
    const uint32_t chromaSourceOffset = chromaOffset * sourceStep + x;
    const uint8_t chromaCb = source[1][chromaSourceOffset];
    const uint8_t chromaCr = source[1][chromaSourceOffset + 1];

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
__device__ __forceinline__ float3 getRGB(const float3 pixel)
{
    // Normalise float values
    return make_float3(pixel.x / 255.0f, pixel.y / 255.0f, pixel.z / 255.0f);
}

template<uint8_t>
__device__ __forceinline__ char3 getRGB(const float3 pixel)
{
    return make_char3(pixel.x, pixel.y, pixel.z);
}

template<typename T>
__global__ void convertNV12ToRGBP(const uint8_t* const source[2], const uint32_t sourceStep, const uint32_t width,
    const uint32_t height, uint8_t* dest[3], const uint32_t destStep)
{
    const uint32_t x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) {
        return;
    }

    Pixel2 pixels = getNV12ToRGB(x, y, source, sourceStep);

    const auto pixel1 = getRGB<T>(pixels.m_pixels[0]);
    const auto pixel2 = getRGB<T>(pixels.m_pixels[1]);
    const uint32_t destOffset = y * destStep + x;
    dest[0][destOffset] = pixel1.x;
    dest[0][destOffset + 1] = pixel2.x;
    dest[1][destOffset] = pixel1.y;
    dest[1][destOffset + 1] = pixel2.y;
    dest[2][destOffset] = pixel1.z;
    dest[2][destOffset + 1] = pixel2.z;
}

__forceinline__ int divUp(const uint32_t total, const uint32_t grain)
{
    return (total + grain - 1) / grain;
}

cudaError_t convertNV12ToRGB8P(const uint8_t* const source[2], const uint32_t sourceStep, const uint32_t width,
    const uint32_t height, uint8_t* dest[3], const uint32_t destStep)
{
    const dim3 blockDim(8, 8, 1);
    const dim3 gridDim(divUp(width, blockDim.x), divUp(height, blockDim.y), 1);

    convertNV12ToRGBP<char3><<<gridDim, blockDim>>>(source, sourceStep, width, height, dest, destStep);

    return cudaPeekAtLastError();
}

cudaError_t convertNV12ToRGB32FP(const uint8_t* const source[2], const uint32_t sourceStep, const uint32_t width,
    const uint32_t height, uint8_t* dest[3], const uint32_t destStep)
{
    const dim3 blockDim(8, 8, 1);
    const dim3 gridDim(divUp(width, blockDim.x), divUp(height, blockDim.y), 1);

    convertNV12ToRGBP<char3><<<gridDim, blockDim>>>(source, sourceStep, width, height, dest, destStep);

    return cudaPeekAtLastError();
}
} // namespace Ffr