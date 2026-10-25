/* Exercise the production M2MF submission helper, including the 3x2 boot
 * self-test shape. The recorder models byte copies, not physical RSX timing. */
#include "../vendor/ps3gl/readback_transfer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char sourceMemory[4 * 1024 * 1024];
static unsigned char destinationMemory[2 * 1024 * 1024];
static unsigned char expected[sizeof(destinationMemory)];
static unsigned modeCalls, transfers;
static s32 inputPitch, outputPitch;
static u32 lineBytes, lineCount;
static bool formatReady, modeReady;

void rsxSetTransferDataMode(gcmContextData* ctx, u8 mode) {
    (void)ctx;
    assert(mode == GCM_TRANSFER_LOCAL_TO_MAIN);
    ++modeCalls; modeReady = true;
}
void rsxSetTransferDataFormat(gcmContextData* ctx, s32 in, s32 out,
                              u32 bytes, u32 rows, u8 inBytes, u8 outBytes) {
    (void)ctx;
    assert(modeReady && !formatReady);
    assert(rows > 0 && rows <= 2047 && bytes > 0 && bytes <= 0x3fffff);
    assert(in >= 0 && in <= 32767 && out >= 0 && out <= 32767);
    assert(rows == 1 || (in >= (s32)bytes && out >= (s32)bytes));
    assert(inBytes == 1 && outBytes == 1);
    inputPitch = in; outputPitch = out; lineBytes = bytes; lineCount = rows;
    formatReady = true;
}
void rsxSetTransferDataOffset(gcmContextData* ctx, u32 dst, u32 src) {
    (void)ctx;
    assert(formatReady);
    assert((uint64_t)src + (lineCount-1) * inputPitch + lineBytes <= sizeof(sourceMemory));
    assert((uint64_t)dst + (lineCount-1) * outputPitch + lineBytes <= sizeof(destinationMemory));
    for (u32 y = 0; y < lineCount; ++y) {
        memcpy(destinationMemory + dst + y * outputPitch,
               sourceMemory + src + y * inputPitch, lineBytes);
    }
    formatReady = false; ++transfers;
    assert(transfers <= 4096); /* A repeated/unterminated submission fails here. */
}

static void check(u32 source, u32 pitch, u32 bytes, u32 rows) {
    const u32 destination = 19;
    memset(destinationMemory, 0xCD, sizeof(destinationMemory));
    memset(expected, 0xCD, sizeof(expected));
    modeCalls = transfers = 0; modeReady = formatReady = false;
    for (u32 y = 0; y < rows; ++y)
        memcpy(expected + destination + y * bytes, sourceMemory + source + y * pitch, bytes);
    assert(ps3glSubmitReadback(NULL, destination, source, pitch, bytes, rows));
    assert(modeCalls == 1 && !formatReady);
    assert(transfers == (pitch > 32767 || bytes > 32767 ? rows : (rows + 2046) / 2047));
    assert(memcmp(destinationMemory, expected, sizeof(expected)) == 0);
}

int main(void) {
    for (size_t i = 0; i < sizeof(sourceMemory); ++i)
        sourceMemory[i] = (unsigned char)((i * 37 + i / 64) & 255);
    check(4, 64, 8, 2);       /* Boot crop: x=1, 3x2 padded texture. */
    check(8, 64, 4, 1);       /* Boot clear/alpha readback. */
    check(0, 320, 300, 50);   /* Temporary 75x50 battle cage. */
    check(12, 64, 44, 3000);  /* Split at the hardware row limit. */
    check(0, 32768, 12, 3);   /* Oversized source pitch: bounded single rows. */
    check(0, 65536, 65536, 3);/* Oversized output pitch too. */
    check(0, 2048, 2048, 512);/* Full 1MiB scratch capacity. */
    u32 seed = 7;
    for (unsigned i = 0; i < 200; ++i) {
        seed = seed * 1664525 + 1013904223;
        u32 bytes = 1 + seed % 2048;
        seed = seed * 1664525 + 1013904223;
        u32 rows = 1 + seed % (1024 * 1024 / bytes);
        if (rows > 3000) rows = 3000;
        u32 pitch = (bytes + 63) & ~63u;
        check(seed % 64, pitch, bytes, rows);
    }
    modeCalls = 0;
    assert(!ps3glSubmitReadback(NULL, 0, 0, 64, 0, 2));
    assert(!ps3glSubmitReadback(NULL, 0, 0, 64, 8, 0));
    assert(!ps3glSubmitReadback(NULL, 0, 0, 4, 8, 2));
    assert(!ps3glSubmitReadback(NULL, 0, 0, 2048, 2048, 513));
    assert(!ps3glSubmitReadback(NULL, 0, UINT32_MAX-8, 64, 8, 2));
    assert(!ps3glSubmitReadback(NULL, UINT32_MAX-8, 0, 64, 8, 2));
    assert(modeCalls == 0);
    puts("PASS: bounded M2MF boot crop, clear, cage, 2047-row split, large pitches, 1MiB, 200 varied copies and overflow rejection");
    return 0;
}
