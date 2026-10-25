#undef ENABLE_VM_TRACING
#include "../src/vm_builtins.c"
#include <assert.h>

static RendererVertex2D captured[128];
static int count,kind,draws;static uint32_t texture;
static float part[10];
static void capture(Renderer* r,uint32_t tex,const RendererVertex2D* v,int n,int mode) {
    (void)r;assert(n<=128);memcpy(captured,v,n*sizeof(*v));count=n;kind=mode;texture=tex;++draws;
}
static uint32_t getTexture(Renderer* r,int id) { (void)r;assert(id==0);return 42; }
static bool getUVs(Renderer* r,uint32_t tex,float* uv) { (void)r;assert(tex==42);uv[0]=.25f;uv[1]=.25f;uv[2]=.5f;uv[3]=.5f;return true; }
static void capturePart(Renderer* r,int id,float l,float t,float w,float h,float x,float y,float xs,float ys,float angle,float px,float py,uint32_t color,float alpha) {
    (void)r;(void)id;(void)angle;(void)px;(void)py;(void)color;(void)alpha;
    float values[]={l,t,w,h,x,y,xs,ys};memcpy(part,values,sizeof(values));++draws;
}
int main(void) {
    RendererVtable vt={.drawTexturedVertices=capture,.spriteGetTexture=getTexture,.textureGetUVs=getUVs,.drawSpritePart=capturePart};
    int32_t page=0;Sprite sprite={.textureCount=1,.tpagIndices=&page};
    TexturePageItem item={.targetX=8,.targetY=4,.targetWidth=16,.targetHeight=12,.sourceWidth=8,.sourceHeight=6};
    DataWin data={.sprt={.count=1,.sprites=&sprite},.tpag={.count=1,.items=&item}};
    Renderer renderer={.vtable=&vt,.dataWin=&data,.drawColor=0x123456,.drawAlpha=.4f,.circlePrecision=24};
    Runner runner={.renderer=&renderer};VMContext ctx={.runner=&runner};
    // Wipe fans preserve vertex colour/alpha and submit only at end.
    RValue begin[]={RValue_makeInt32(6)};builtin_draw_primitive_begin(&ctx,begin,1);
    for(int i=0;i<80;++i) { RValue v[]={RValue_makeInt32(i),RValue_makeInt32(2*i)};builtin_draw_vertex(&ctx,v,2); }
    assert(draws==0);builtin_draw_primitive_end(&ctx,NULL,0);
    assert(draws==1&&count==80&&kind==6&&texture==0);
    assert(captured[79].x==79&&captured[79].y==158&&captured[79].color==0x123456&&captured[79].alpha==.4f);
    builtin_draw_primitive_end(&ctx,NULL,0);assert(draws==1);
    RValue tb[]={RValue_makeInt32(5),RValue_makeInt32(42)};builtin_draw_primitive_begin(&ctx,tb,2);
    RValue v[]={RValue_makeInt32(5),RValue_makeInt32(7),RValue_makeReal(.2),RValue_makeReal(.8),RValue_makeInt32(0xABCDEF),RValue_makeReal(.3)};
    builtin_draw_vertex_texture_color(&ctx,v,6);builtin_draw_primitive_end(&ctx,NULL,0);
    assert(count==1&&texture==42&&kind==5&&captured[0].color==0xABCDEF&&fabsf(captured[0].alpha-.3f)<.0001f);
    // Trimmed, downscaled atlas entries still occupy their original GML size.
    Renderer_drawSpritePartExt(&renderer,0,0,0,0,32,24,100,200,2,3,0,0,0,0xFFFFFF,1);
    assert(part[0]==0&&part[1]==0&&part[2]==8&&part[3]==6);
    assert(part[4]==116&&part[5]==212&&part[6]==4&&part[7]==6);
    Renderer_drawSpriteGeneral(&renderer,0,0,8,4,16,12,100,200,-2,3,90,0xFF,0xFF00,0xFF0000,0xFFFFFF,.5f);
    assert(count==4&&kind==5&&texture==42&&captured[0].color==0xFF&&captured[3].color==0xFF0000);
    assert(fabsf(captured[3].x-136)<.001f&&fabsf(captured[3].y-232)<.001f);
    assert(captured[0].u==.25f&&captured[3].u==.5f&&captured[3].v==.5f);
    // Each circle is one batch instead of 24 separate RSX draws.
    int before=draws;Renderer_drawCircleColor(&renderer,10,20,8,0xFF,0xFF00,false);
    assert(draws==before+1&&count==26&&kind==6&&captured[0].color==0xFF&&captured[1].color==0xFF00);
    Renderer_drawCircle(&renderer,10,20,8,true);assert(count==25&&kind==3);
    arrfree(runner.primitiveVertices);
    puts("PASS: wipe fans, textured/color vertices, atlas scale/trim, rotated four-corner gradients and batched circles");
}
