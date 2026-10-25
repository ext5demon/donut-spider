#include "pixel_layout.h"
#include "asset_offset_index.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    int s0 = 0, s1 = 10, d0 = -2, d1 = 8;
    assert(ps3glClipCopyAxis(&s0,&s1,&d0,&d1,10,5));
    assert(s0 == 2 && s1 == 7 && d0 == 0 && d1 == 5);
    s0 = -2; s1 = 3; d0 = 1; d1 = 6;
    assert(ps3glClipCopyAxis(&s0,&s1,&d0,&d1,4,6));
    assert(s0 == 0 && s1 == 3 && d0 == 3 && d1 == 6);
    s0 = 0; s1 = 5; d0 = 7; d1 = 12;
    assert(!ps3glClipCopyAxis(&s0,&s1,&d0,&d1,5,6));
    const uint8_t rgba[] = {
        10,20,30,0, 40,50,60,64, 70,80,90,128,
        11,21,31,192, 41,51,61,254, 71,81,91,255
    };
    uint8_t storage[128], output[32];
    memset(storage, 0xCD, sizeof(storage));
    assert(ps3glPixelPitch(3,4) == 64);
    assert(ps3glPixelPitch(65,1) == 128);
    ps3glUploadPixels(storage,64,rgba,3,2,4);
    assert(storage[0] == 0 && storage[1] == 10 && storage[64] == 192);
    assert(storage[12] == 0xCD && storage[76] == 0xCD);
    memset(output,0xEE,sizeof(output));
    ps3glReadPixelsRGBA(output,storage,64,3,2,1,0,2,2);
    assert(memcmp(output,rgba+16,8) == 0);
    assert(memcmp(output+8,rgba+4,8) == 0);
    assert(output[16] == 0xEE);
    ps3glReadPixelsRGBA(output,storage,64,3,2,0,1,1,1);
    assert(memcmp(output,rgba,4) == 0);
    ps3glReadPixelsRGBA(output,storage,64,3,2,-1,0,2,1);
    assert(output[0] == 0 && output[1] == 0 && output[2] == 0 && output[3] == 0);
    assert(memcmp(output+4,rgba+12,4) == 0);
    const uint8_t rgb[] = {1,2,3,4,5,6};
    ps3glUploadPixels(storage,64,rgb,1,2,3);
    assert(storage[0] == 255 && storage[1] == 1 && storage[64] == 255 && storage[65] == 4);
    const uint8_t indices[] = {4,8,12,16,20,24};
    ps3glUploadPixels(storage,64,indices,3,2,1);
    assert(memcmp(storage,indices,3) == 0 && memcmp(storage+64,indices+3,3) == 0);

    /* Non-monotonic pointers must retain their original TPAG/palette identity. */
    AssetOffsetIndex lookup[] = {{300,0},{100,1},{0,2},{200,3},{100,4}};
    qsort(lookup,5,sizeof(*lookup),AssetOffsetIndex_compare);
    assert(AssetOffsetIndex_find(lookup,5,300) == 0);
    assert(AssetOffsetIndex_find(lookup,5,100) == 1);
    assert(AssetOffsetIndex_find(lookup,5,200) == 3);
    assert(AssetOffsetIndex_find(lookup,5,0) == -1);
    assert(AssetOffsetIndex_find(lookup,5,101) == -1);
    assert(AssetOffsetIndex_find(NULL,0,100) == -1);
    puts("PASS: padded RGB/RGBA/index transfers, alpha, cropped readback, bounds, TPAG identities");
    return 0;
}
