#ifndef PS3GL_PIXEL_LAYOUT_H
#define PS3GL_PIXEL_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Clip a 1:1 copy without changing the source/destination pixel correspondence. */
static inline bool ps3glClipCopyAxis(int* s0, int* s1, int* d0, int* d1, int srcSize, int dstSize) {
    int64_t start = 0, end = (int64_t)*s1 - *s0;
    if (-(int64_t)*s0 > start) start = -(int64_t)*s0;
    if (-(int64_t)*d0 > start) start = -(int64_t)*d0;
    if ((int64_t)srcSize - *s0 < end) end = (int64_t)srcSize - *s0;
    if ((int64_t)dstSize - *d0 < end) end = (int64_t)dstSize - *d0;
    if (start >= end) return false;
    *s1 = (int)((int64_t)*s0 + end);
    *d1 = (int)((int64_t)*d0 + end);
    *s0 = (int)((int64_t)*s0 + start);
    *d0 = (int)((int64_t)*d0 + start);
    return true;
}

/* RSX linear textures/render targets require padded rows. Keep the conversion
 * independent of RSX so padded, non-square and cropped transfers can be tested. */
static inline size_t ps3glPixelPitch(unsigned width, unsigned bytesPerPixel) {
    return ((size_t)width * bytesPerPixel + 63u) & ~(size_t)63u;
}

/* All colour textures use native A8R8G8B8, including textures attached to FBOs.
 * A framebuffer attachment must not change the interpretation of uploaded data. */
static inline void ps3glUploadPixels(uint8_t* dst, size_t pitch,
                                     const uint8_t* src, unsigned width,
                                     unsigned height, unsigned components) {
    for (unsigned y = 0; y < height; ++y) {
        uint8_t* row = dst + (size_t)y * pitch;
        const uint8_t* input = src + (size_t)y * width * components;
        if (components == 1) {
            memcpy(row, input, width);
        } else {
            for (unsigned x = 0; x < width; ++x) {
                row[4*x] = components == 4 ? input[components*x+3] : 255;
                row[4*x+1] = input[components*x];
                row[4*x+2] = input[components*x+1];
                row[4*x+3] = input[components*x+2];
            }
        }
    }
}

/* glReadPixels uses lower-left coordinates and returns the bottom row first;
 * RSX render targets are stored top-down. Out-of-target pixels are zeroed. */
static inline void ps3glReadPixelsRGBA(uint8_t* dst, const uint8_t* src,
                                      size_t pitch, unsigned targetW,
                                      unsigned targetH, int x, int y,
                                      unsigned width, unsigned height) {
    for (unsigned row = 0; row < height; ++row) {
        int64_t sy = (int64_t)targetH - 1 - ((int64_t)y + row);
        for (unsigned col = 0; col < width; ++col) {
            uint8_t* out = dst + ((size_t)row * width + col) * 4;
            int64_t sx = (int64_t)x + col;
            if (sx < 0 || sx >= targetW || sy < 0 || sy >= targetH) {
                memset(out, 0, 4);
            } else {
                const uint8_t* in = src + (size_t)sy * pitch + (size_t)sx * 4;
                out[0] = in[1]; out[1] = in[2]; out[2] = in[3]; out[3] = in[0];
            }
        }
    }
}

#endif
