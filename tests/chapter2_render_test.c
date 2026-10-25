#include "renderer.h"
#include "quad_visibility.h"
#include "palette_swap.h"
#include <assert.h>
#include <stdio.h>
static float recorded[6];
static void part(Renderer* r,int32_t t,float l,float top,float w,float h,float x,float y,float sx,float sy,float angle,float px,float py,uint32_t c,float a) {
    (void)r;(void)t;(void)sx;(void)sy;(void)angle;(void)px;(void)py;(void)c;(void)a;
    recorded[0]=l;recorded[1]=top;recorded[2]=w;recorded[3]=h;recorded[4]=x;recorded[5]=y;
}
int main(void) {
    DataWin dw={0};Renderer renderer={0};RendererVtable api={.drawSpritePart=part};
    int32_t tp=0;Sprite sprite={.textureCount=1,.tpagIndices=&tp};
    TexturePageItem item={.sourceWidth=100,.sourceHeight=100,.targetWidth=100,.targetHeight=100};
    dw.sprt.count=1;dw.sprt.sprites=&sprite;dw.tpag.count=1;dw.tpag.items=&item;
    renderer.dataWin=&dw;renderer.vtable=&api;
    // Queen's perspective strips must not truncate a subpixel width to zero.
    Renderer_drawSpritePartExt(&renderer,0,0,3.25f,0,.375f,2,10,20,2,1,0,0,0,0xffffff,1);
    assert(recorded[0]==3.25f&&recorded[2]==.375f&&recorded[4]==10);
    item.targetX=2;item.targetY=3;
    Renderer_drawSpritePartExt(&renderer,0,0,1.75f,2.5f,1.25f,2,10,20,-2,3,0,0,0,0xffffff,1);
    assert(recorded[0]==0&&recorded[1]==0&&recorded[2]==1&&recorded[3]==1.5f&&recorded[4]==9.5f&&recorded[5]==21.5f);
    float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    assert(quadVisible(m,-2,0,2,0,2,.5f,-2,.5f)); // crosses viewport; all corners outside
    assert(!quadVisible(m,2,0,3,0,3,.5f,2,.5f));
    assert(quadVisible(m,1,0,1.001f,0,1.001f,1,1,1));
    m[12]=-2;assert(quadVisible(m,2,0,3,0,3,.5f,2,.5f)); // camera/world translation
    m[15]=-1;assert(quadVisible(m,20,0,30,0,30,5,20,5)); // uncertain perspective
    m[0]=NAN;assert(quadVisible(m,20,0,30,0,30,5,20,5));
    uint8_t source[1024]={0},mapped[1024],palette[]={10,20,30,255, 110,120,130,255, 210,220,230,255,
                                                                1,2,3,255,4,5,6,255,7,8,9,255};
    memcpy(source+4,palette,4);source[4]++; // within .004 tolerance
    memcpy(source+8,palette+12,4);source[12]=99;source[15]=255;
    paletteSwapRow(mapped,source,palette,3,2,0,0,2,1.5f);
    assert(mapped[4]==160&&mapped[5]==170&&mapped[6]==180&&mapped[7]==255);
    assert(mapped[8]==6&&mapped[9]==7&&mapped[10]==8&&mapped[11]==255);
    assert(mapped[12]==99&&mapped[15]==255);assert(!memcmp(mapped,source,4));
    puts("PASS: fractional/mirrored trimmed strips, conservative transformed culling, exact/interpolated palette substitution and alpha");
}
