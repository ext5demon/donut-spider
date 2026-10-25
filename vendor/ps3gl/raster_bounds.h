#ifndef PS3GL_RASTER_BOUNDS_H
#define PS3GL_RASTER_BOUNDS_H

#include <stdint.h>

typedef struct {
    uint16_t x, y, w, h;
} Ps3glRasterRect;

/* GL rectangles are signed and bottom-up. RSX clip origins are unsigned and
 * top-down. Intersect before narrowing, including viewports left over from a
 * larger target. Clipping must not change the viewport's scale/offset. */
static inline Ps3glRasterRect ps3glClipRasterRect(int x, int y, int w, int h,
                                                unsigned targetW, unsigned targetH) {
    int64_t left = x, top = (int64_t)targetH - y - h;
    int64_t right = left + w, bottom = top + h;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (left > targetW) left = targetW;
    if (top > targetH) top = targetH;
    if (right > targetW) right = targetW;
    if (bottom > targetH) bottom = targetH;
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    return (Ps3glRasterRect){(uint16_t)left, (uint16_t)top,
                             (uint16_t)(right - left), (uint16_t)(bottom - top)};
}

static inline void ps3glViewportTransform(int x, int y, int w, int h,
                                         unsigned targetH, float nearZ, float farZ,
                                         float scale[4], float offset[4]) {
    scale[0] = w * 0.5f;
    scale[1] = h * -0.5f;
    scale[2] = (farZ - nearZ) * 0.5f;
    scale[3] = 0.0f;
    offset[0] = (float)x + w * 0.5f;
    offset[1] = (float)((int64_t)targetH - y) - h * 0.5f;
    offset[2] = (farZ + nearZ) * 0.5f;
    offset[3] = 0.0f;
}

#endif
