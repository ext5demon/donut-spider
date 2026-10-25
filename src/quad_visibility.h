#ifndef SP_QUAD_VISIBILITY_H
#define SP_QUAD_VISIBILITY_H
#include <math.h>
#include <stdbool.h>

// Column-major world/view/projection. Keep uncertain perspective cases and a
// small clip-space margin so clipping cannot remove edge raster coverage.
static inline bool quadVisible(const float* m, float x0,float y0,float x1,float y1,
                               float x2,float y2,float x3,float y3) {
    const float xy[8]={x0,y0,x1,y1,x2,y2,x3,y3};
    unsigned outside=15;
    for(unsigned i=0;i<4;++i) {
        float x=xy[2*i],y=xy[2*i+1];
        float cx=m[0]*x+m[4]*y+m[12];
        float cy=m[1]*x+m[5]*y+m[13];
        float w=m[3]*x+m[7]*y+m[15];
        if(!isfinite(cx)||!isfinite(cy)||!isfinite(w)||w<=0) return true;
        float bound=w*1.005f;
        outside &= (cx < -bound ? 1u:0u) | (cx > bound ? 2u:0u)
                 | (cy < -bound ? 4u:0u) | (cy > bound ? 8u:0u);
    }
    return outside==0;
}
#endif
