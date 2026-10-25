#include "ps3_textures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps3_session_log.h"

// We stream the texture pages on demand from the file instead of loading everything in RAM.

#define PAGE_HEADER_SIZE 12  // u16 w, u16 h, u32 pixelOffset, u32 pixelDataSize

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pixelOffset;
    uint32_t pixelDataSize;
} PageInfo;

static FILE*       gFp;
static uint16_t    gClutCount;
static uint16_t    gPageCount;
static uint16_t    gTpagCount;
static uint16_t*   gTpagClutMap;     // [tpagCount]
static PageInfo*   gPageInfo;        // [pageCount]
static long        gPixelBlockBase;  // file offset where pixel block begins
static GLuint      gClutTexture;
static bool        gInitialized;

// TEXTURES.BIN is written big-endian to match PS3's native byte order. On
// PPC this lets the runner read multi-byte fields without swapping.
static inline uint16_t readU16BE(const uint8_t* p) {
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static inline uint32_t readU32BE(const uint8_t* p) {
    return ((uint32_t) p[0] << 24)
         | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8)
         |  (uint32_t) p[3];
}

bool PS3Textures_init(const char* texturesBinPath) {
    if (gInitialized)
        return true;

    gFp = fopen(texturesBinPath, "rb");
    if (gFp == NULL) {
        fprintf(stderr, "PS3Textures: cannot open %s\n", texturesBinPath);
        return false;
    }

    // --- 7-byte header ---
    uint8_t headerBuf[7];
    if (fread(headerBuf, 1, 7, gFp) != 7) goto fail;
    if (headerBuf[0] != 0) {
        fprintf(stderr, "PS3Textures: unsupported version %u\n", headerBuf[0]);
        goto fail;
    }
    gClutCount = readU16BE(headerBuf + 1);
    gPageCount = readU16BE(headerBuf + 3);
    gTpagCount = readU16BE(headerBuf + 5);
    if (!gClutCount || gClutCount > 4096 || !gPageCount || !gTpagCount) goto fail;

    PS3SessionLog_event("PS3Textures_init: gTpagCount=%u gClutCount=%u", gTpagCount, gClutCount);

    // --- CLUT atlas: read into a temp buffer, upload to GPU, free ---
    size_t clutBytes = (size_t) gClutCount * 256 * 4;
    uint8_t* clutBuf = (uint8_t*) malloc(clutBytes);
    if (clutBuf == NULL) {
        fprintf(stderr, "PS3Textures: malloc(%zu) for CLUT failed\n", clutBytes);
        goto fail;
    }
    if (fread(clutBuf, 1, clutBytes, gFp) != clutBytes) {
        free(clutBuf);
        goto fail;
    }
    glGenTextures(1, &gClutTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gClutTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, gClutCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, clutBuf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    free(clutBuf);
    if (!ps3glTextureReady(gClutTexture)) goto fail;

    // --- TPAG -> CLUT row map (small, kept resident) ---
    size_t mapBytes = (size_t) gTpagCount * 2;
    uint8_t* mapBuf = (uint8_t*) malloc(mapBytes);
    if (mapBuf == NULL) goto fail;
    if (fread(mapBuf, 1, mapBytes, gFp) != mapBytes) { free(mapBuf); goto fail; }
    gTpagClutMap = (uint16_t*) malloc(gTpagCount * sizeof(uint16_t));
    if (gTpagClutMap == NULL) { free(mapBuf); goto fail; }
    for (uint32_t i = 0; i < gTpagCount; i++) {
        gTpagClutMap[i] = readU16BE(mapBuf + i * 2);
    }
    free(mapBuf);
    for (uint32_t i = 0; i < gTpagCount; i++) {
        if (gTpagClutMap[i] != 0xFFFF && gTpagClutMap[i] >= gClutCount) goto fail;
    }

    // --- Page header table (small, kept resident) ---
    size_t headerBytes = (size_t) gPageCount * PAGE_HEADER_SIZE;
    uint8_t* hdrBuf = (uint8_t*) malloc(headerBytes);
    if (hdrBuf == NULL) goto fail;
    if (fread(hdrBuf, 1, headerBytes, gFp) != headerBytes) { free(hdrBuf); goto fail; }
    gPageInfo = (PageInfo*) malloc(gPageCount * sizeof(PageInfo));
    if (gPageInfo == NULL) { free(hdrBuf); goto fail; }
    for (uint32_t i = 0; i < gPageCount; i++) {
        const uint8_t* p = hdrBuf + i * PAGE_HEADER_SIZE;
        gPageInfo[i].width         = readU16BE(p + 0);
        gPageInfo[i].height        = readU16BE(p + 2);
        gPageInfo[i].pixelOffset   = readU32BE(p + 4);
        gPageInfo[i].pixelDataSize = readU32BE(p + 8);
    }
    free(hdrBuf);

    // Pixel block starts here. Pages are streamed from disk on demand.
    gPixelBlockBase = ftell(gFp);
    if (gPixelBlockBase < 0 || fseek(gFp, 0, SEEK_END) != 0) goto fail;
    long fileSize = ftell(gFp);
    if (fileSize < gPixelBlockBase) goto fail;
    for (uint32_t i = 0; i < gPageCount; i++) {
        const PageInfo* page = &gPageInfo[i];
        if (page->pixelDataSize == 0) continue; // unused/empty pages are valid
        if (!page->width || !page->height || page->width > 4096 || page->height > 4096 ||
            (uint64_t)page->width * page->height != page->pixelDataSize ||
            (uint64_t)page->pixelOffset + page->pixelDataSize > (uint64_t)(fileSize - gPixelBlockBase)) {
            PS3SessionLog_event("Invalid TEXTURES.BIN page %u: %ux%u offset=%u bytes=%u", i,
                                page->width, page->height, page->pixelOffset, page->pixelDataSize);
            goto fail;
        }
    }

    fprintf(stderr, "PS3Textures: opened %s (clutCount=%u pages=%u tpags=%u, streaming pixels)\n", texturesBinPath, gClutCount, gPageCount, gTpagCount);

    gInitialized = true;
    return true;

fail:
    PS3SessionLog_event("TEXTURES.BIN invalid, truncated, or GPU allocation failed: %s", texturesBinPath);
    if (gFp != NULL) { fclose(gFp); gFp = NULL; }
    if (gClutTexture != 0) glDeleteTextures(1, &gClutTexture);
    free(gTpagClutMap);
    free(gPageInfo);
    gTpagClutMap = NULL;
    gPageInfo = NULL;
    gClutTexture = 0;
    gClutCount = gPageCount = gTpagCount = 0;
    gPixelBlockBase = 0;
    return false;
}

void PS3Textures_free(void) {
    if (!gInitialized)
        return;

    glDeleteTextures(1, &gClutTexture);

    if (gFp != NULL)
        fclose(gFp);

    free(gTpagClutMap);
    free(gPageInfo);
    gFp = NULL;
    gTpagClutMap = NULL;
    gPageInfo = NULL;
    gClutTexture = 0;
    gClutCount = gPageCount = gTpagCount = 0;
    gInitialized = false;
}

uint32_t PS3Textures_getPageCount() {
    return gInitialized ? (uint32_t) gPageCount : 0;
}

bool PS3Textures_loadPage(uint32_t pageId, int* outW, int* outH, uint8_t** outPixels) {
    if (!outW || !outH || !outPixels) return false;
    *outW = *outH = 0;
    *outPixels = NULL;
    if (!gInitialized || pageId >= gPageCount) return false;
    const PageInfo* h = &gPageInfo[pageId];
    if (h->width == 0 || h->height == 0 || h->pixelDataSize == 0) return false;

    PS3SessionLog_event("[OK] Reading page %u from file (offset %ld, %u bytes)", pageId, gPixelBlockBase + (long) h->pixelOffset, h->pixelDataSize);

    uint8_t* buf = (uint8_t*) malloc(h->pixelDataSize);
    if (buf == NULL) {
        fprintf(stderr, "PS3Textures: malloc(%u) for page %u failed\n", h->pixelDataSize, pageId);
        PS3SessionLog_event("[ :( ] malloc failed for page %u", pageId);
        return false;
    }

    if (fseek(gFp, gPixelBlockBase + (long) h->pixelOffset, SEEK_SET) != 0) {
        free(buf);
        PS3SessionLog_event("[ :( ] fseek failed for page %u", pageId);
        return false;
    }
    if (fread(buf, 1, h->pixelDataSize, gFp) != h->pixelDataSize) {
        free(buf);
        PS3SessionLog_event("[ :( ] fread failed for page %u", pageId);
        return false;
    }

    *outW = (int) h->width;
    *outH = (int) h->height;
    *outPixels = buf;
    return true;
}

bool PS3Textures_validate(const DataWin* dataWin) {
    if (!gInitialized || !dataWin || dataWin->tpag.count != gTpagCount) {
        PS3SessionLog_event("Texture bundle mismatch: WAD TPAGs=%u bundle TPAGs=%u; rebuild TEXTURES.BIN from this data.win",
                            dataWin ? dataWin->tpag.count : 0, gTpagCount);
        return false;
    }
    for (uint32_t i = 0; i < dataWin->tpag.count; ++i) {
        const TexturePageItem* item = &dataWin->tpag.items[i];
        if (!item->present || item->texturePageId < 0 || !item->sourceWidth || !item->sourceHeight) continue;
        uint32_t pageId = (uint32_t)item->texturePageId;
        if (pageId >= gPageCount || gTpagClutMap[i] == 0xFFFF ||
            !gPageInfo[pageId].pixelDataSize ||
            (uint32_t)item->sourceX + item->sourceWidth > gPageInfo[pageId].width ||
            (uint32_t)item->sourceY + item->sourceHeight > gPageInfo[pageId].height) {
            PS3SessionLog_event("Texture bundle mismatch at TPAG=%u page=%u; rebuild bundle for this WAD", i, pageId);
            return false;
        }
    }
    return true;
}

GLuint PS3Textures_getClutTexture() {
    return gInitialized ? gClutTexture : 0;
}

float PS3Textures_getTpagPaletteV(int32_t tpagIndex) {
    if (!gInitialized) return -1.0f;
    if (tpagIndex < 0 || (uint32_t) tpagIndex >= gTpagCount) return -1.0f;
    uint16_t row = gTpagClutMap[tpagIndex];
    if (row == 0xFFFF || row >= gClutCount) return -1.0f;
    return ((float) row + 0.5f) / (float) gClutCount;
}

bool PS3Textures_readClut(int32_t tpagIndex, uint8_t out[1024]) {
    if (!out || !gInitialized || tpagIndex < 0 || (uint32_t)tpagIndex >= gTpagCount) return false;
    uint16_t row=gTpagClutMap[tpagIndex];
    return row < gClutCount && fseek(gFp,7L+(long)row*1024,SEEK_SET)==0 && fread(out,1,1024,gFp)==1024;
}

// Used only for the small color-table sprite requested by a GML palette shader.
// No full-page decode/upload or GPU readback is needed to look up these colors.
bool PS3Textures_readTpagRGBA(const TexturePageItem* item,int32_t tpagIndex,uint8_t** out) {
    if(!out)return false;
    *out=NULL;
    if(!gInitialized||!item||item->texturePageId<0||(uint32_t)item->texturePageId>=gPageCount)return false;
    const PageInfo* page=&gPageInfo[item->texturePageId];
    size_t count=(size_t)item->sourceWidth*item->sourceHeight;
    if(!count||count>256u*1024u||item->sourceX+item->sourceWidth>page->width||
       item->sourceY+item->sourceHeight>page->height)return false;
    uint8_t colors[1024];if(!PS3Textures_readClut(tpagIndex,colors))return false;
    uint8_t* pixels=malloc(count*4);uint8_t* row=malloc(item->sourceWidth);
    if(!pixels||!row){free(pixels);free(row);return false;}
    for(unsigned y=0;y<item->sourceHeight;++y) {
        long offset=gPixelBlockBase+page->pixelOffset+(long)(item->sourceY+y)*page->width+item->sourceX;
        if(fseek(gFp,offset,SEEK_SET)!=0||fread(row,1,item->sourceWidth,gFp)!=item->sourceWidth){free(pixels);free(row);return false;}
        for(unsigned x=0;x<item->sourceWidth;++x)memcpy(pixels+((size_t)y*item->sourceWidth+x)*4,colors+row[x]*4,4);
    }
    free(row);*out=pixels;return true;
}
