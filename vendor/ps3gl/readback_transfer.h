#ifndef PS3GL_READBACK_TRANSFER_H
#define PS3GL_READBACK_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>
#include <rsx/rsx.h>

/* Do not use rsxSetTransferData here. The bundled PSL1GHT implementation
 * compares unsigned pitches with -32768 and loops on --unsignedRows >= 0.
 * Its generated PPU code repeats the same row forever. Submit the documented
 * M2MF primitives directly, with bounded rows and explicit offset advances. */
static inline bool ps3glSubmitReadback(gcmContextData* ctx, uint32_t destination,
                                      uint32_t source, uint32_t pitch,
                                      uint32_t rowBytes, uint32_t rows) {
    if (!rowBytes || !rows || pitch < rowBytes ||
        (uint64_t)rowBytes * rows > 1024u * 1024u ||
        (uint64_t)source + (uint64_t)(rows - 1) * pitch + rowBytes > UINT64_C(0x100000000) ||
        (uint64_t)destination + (uint64_t)rowBytes * rows > UINT64_C(0x100000000)) return false;

    rsxSetTransferDataMode(ctx, GCM_TRANSFER_LOCAL_TO_MAIN);
    for (uint32_t row = 0; row < rows;) {
        uint32_t count = rows - row;
        /* M2MF accepts at most 2047 rows and signed 16-bit pitches. For
         * larger pitches, one row needs no pitch at all. */
        if (pitch > 32767 || rowBytes > 32767) count = 1;
        else if (count > 2047) count = 2047;
        rsxSetTransferDataFormat(ctx, count == 1 ? 0 : (int32_t)pitch,
                                count == 1 ? 0 : (int32_t)rowBytes,
                                rowBytes, count, 1, 1);
        rsxSetTransferDataOffset(ctx, destination + row * rowBytes, source + row * pitch);
        row += count;
    }
    return true;
}

#endif
