#ifndef SP_PALETTE_SWAP_H
#define SP_PALETTE_SWAP_H
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

// Equivalent to shd_pal_swapper's distance < .004 comparison and column
// interpolation. Both atlases use RGBA bytes; never interpolate index values.
static inline void paletteSwapRow(uint8_t out[1024], const uint8_t source[1024],
                                  const uint8_t* palette, unsigned w, unsigned h,
                                  unsigned firstX,unsigned firstY,unsigned endY,
                                  float paletteId) {
    memcpy(out,source,1024);
    if(!palette||!w||!h||firstX>=w||!isfinite(paletteId)||paletteId < -65536.f||paletteId > 65536.f) return;
    if(endY>h)endY=h;
    int low=(int)floorf(paletteId), high=low+1;
    float fraction=paletteId-floorf(paletteId);
    for(unsigned i=0;i<256;++i) {
        const uint8_t* c=source+i*4;
        if(c[3]==0){memset(out+i*4,0,4);continue;}
        for(unsigned y=firstY;y<endY;++y) {
            const uint8_t* original=palette+((size_t)y*w+firstX)*4;
            unsigned distance=0;
            for(unsigned ch=0;ch<4;++ch){int d=(int)c[ch]-original[ch];distance+=d*d;}
            if(distance>1)continue;
            int x0=(int)firstX+low,x1=(int)firstX+high;
            if(x0<0)x0=0;
            if(x0>=(int)w)x0=w-1;
            if(x1<0)x1=0;
            if(x1>=(int)w)x1=w-1;
            const uint8_t* a=palette+((size_t)y*w+x0)*4;
            const uint8_t* b=palette+((size_t)y*w+x1)*4;
            for(unsigned ch=0;ch<4;++ch) out[i*4+ch]=(uint8_t)lroundf(a[ch]+(b[ch]-a[ch])*fraction);
            break;
        }
    }
}
#endif
