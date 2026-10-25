#ifndef ASSET_OFFSET_INDEX_H
#define ASSET_OFFSET_INDEX_H
#include <stdint.h>
#include <stdlib.h>

typedef struct { uint32_t offset, index; } AssetOffsetIndex;

static int AssetOffsetIndex_compare(const void* left, const void* right) {
    const AssetOffsetIndex* a = (const AssetOffsetIndex*)left;
    const AssetOffsetIndex* b = (const AssetOffsetIndex*)right;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return a->index < b->index ? -1 : a->index != b->index;
}

static int32_t AssetOffsetIndex_find(const AssetOffsetIndex* entries, uint32_t count, uint32_t offset) {
    uint32_t lo = 0, hi = count;
    if (offset == 0) return -1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (entries[mid].offset < offset) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && entries[lo].offset == offset ? (int32_t)entries[lo].index : -1;
}
#endif
