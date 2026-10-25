/* Exercise the production sprite parser's big-endian branches on a host.
 * Scalar reads remain host-correct: only the parser sees IS_BIG_ENDIAN.
 * Collision masks are byte arrays and must be identical on both CPU types. */
#include "binary_utils.h"
#ifndef IS_BIG_ENDIAN
#define IS_BIG_ENDIAN
#endif
#include "../src/data_win.c"
#include "collision.h"
#include <assert.h>

static void put32(uint8_t* bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[at + i] = (uint8_t)(value >> (i * 8));
}

static void testMasks(bool mapped) {
    uint8_t bytes[84] = {0};
    /* One 9x3 sprite, two six-byte frames: masks are NOT individually padded.
     * Frame 0 is a diagonal; frame 1 is a line in the final partial byte. */
    const uint8_t expected[] = {0x80,0, 0xc0,0, 0xe0,0, 0,0x80, 0,0x80, 0,0x80};
    put32(bytes, 0, 1); put32(bytes, 4, 8);
    put32(bytes, 8 + 4, 9); put32(bytes, 8 + 8, 3);
    put32(bytes, 8 + 16, 8); put32(bytes, 8 + 20, 2);
    put32(bytes, 8 + 44, 1); /* precise masks */
    put32(bytes, 68, 2);
    memcpy(bytes + 72, expected, sizeof(expected));

    FILE* file = tmpfile(); assert(file);
    assert(fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    rewind(file);
    BinaryReader reader = BinaryReader_create(file, sizeof(bytes));
    DataWin dw = {0};
    if (mapped) {
        dw.mappedFile = bytes;
        BinaryReader_setBuffer(&reader, bytes, 0, sizeof(bytes));
    }
    parseSPRT(&reader, &dw, true);
    Sprite* spr = &dw.sprt.sprites[0];
    assert(spr->maskCount == 2 && spr->maskWidth == 9 && spr->maskHeight == 3);
    assert(memcmp(spr->masks[0], expected, 6) == 0);
    assert(memcmp(spr->masks[1], expected + 6, 6) == 0);
    if (mapped) assert(spr->masks[0] == bytes + 72);
    Instance instance = {0};
    instance.imageXscale = instance.imageYscale = 1;
    for (int frame = 0; frame < 2; ++frame) {
        instance.imageIndex = frame;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 9; ++x) {
                bool solid = frame == 0 ? x <= y : x == 8;
                assert(Collision_pointInInstance(spr, &instance, x + 0.5, y + 0.5) == solid);
            }
        }
    }
    if (!mapped) {
        free(spr->masks[0]); free(spr->masks[1]);
    }
    free(spr->masks); free(dw.sprt.sprites);
    fclose(file);
}

int main(void) {
    testMasks(false);
    testMasks(true);
    puts("PASS: streamed and mapped big-endian sprite masks preserve diagonals, partial bytes and frame boundaries");
    return 0;
}
