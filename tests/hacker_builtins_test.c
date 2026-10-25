#undef ENABLE_VM_TRACING
#include "../src/vm_builtins.c"
#include <assert.h>

static bool readPixel(Renderer* r, int32_t id, int32_t x, int32_t y, uint8_t out[4]) {
    (void)r;
    if (id != 7 || x != 2 || y != 3) return false;
    const uint8_t expected[] = {0x12, 0x34, 0x56, 0xFE};
    memcpy(out, expected, 4);
    return true;
}
static int lines;
static float segments[4][4];
static float rectangle[4];
static int triangles;
static float triangle[6];
static void drawTriangle(Renderer* r, float x1,float y1,float x2,float y2,float x3,float y3,uint32_t c1,uint32_t c2,uint32_t c3,float alpha,bool outline) {
    (void)r;assert(c1==0x654321 && c2==c1 && c3==c1 && alpha==0.5f && !outline);
    float vertices[]={x1,y1,x2,y2,x3,y3};memcpy(triangle,vertices,sizeof(vertices));++triangles;
}
static void drawRectangle(Renderer* r, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    (void)r;(void)color;(void)alpha;(void)outline;
    float bounds[]={x1,y1,x2,y2};memcpy(rectangle,bounds,sizeof(bounds));
}
static void drawRectangleColor(Renderer* r, float x1, float y1, float x2, float y2, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, float alpha, bool outline) {
    (void)c2;(void)c3;(void)c4;
    drawRectangle(r,x1,y1,x2,y2,c1,alpha,outline);
}
static void drawLine(Renderer* r, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    (void)r;
    assert(lines < 4 && width == 1 && color == 0x654321 && alpha == 0.5f);
    float line[] = {x1,y1,x2,y2};
    memcpy(segments[lines++],line,sizeof(line));
}

int main(void) {
    RendererVtable vtable = {.surfaceGetPixel=readPixel, .drawLine=drawLine,.drawTriangle=drawTriangle,.drawRectangle=drawRectangle,.drawRectangleColor=drawRectangleColor};
    Renderer renderer = {.vtable=&vtable, .drawColor=0x654321, .drawAlpha=0.5f};
    DataWin data = {0};
    Runner runner = {.renderer=&renderer, .dataWin=&data};
    VMContext ctx = {.runner=&runner};
    RValue args[4] = {RValue_makeInt32(7),RValue_makeInt32(2),RValue_makeInt32(3)};
    assert(RValue_toInt64(builtin_surface_getpixel_ext(&ctx,args,3)) == 0xFE563412LL);
    assert(RValue_toInt64(builtin_surface_getpixel(&ctx,args,3)) == 0x563412);
    args[0] = RValue_makeInt32(-1);
    assert(RValue_toInt64(builtin_surface_getpixel_ext(&ctx,args,3)) == 0);

    InternalPathPoint points[] = {{.x=10,.y=20},{.x=30,.y=20},{.x=30,.y=40},{.x=10,.y=20}};
    GamePath path = {.internalPoints=points, .internalPointCount=4};
    data.path.paths=&path; data.path.count=1;
    args[0]=RValue_makeInt32(0); args[1]=RValue_makeInt32(100); args[2]=RValue_makeInt32(200);
    args[3]=RValue_makeBool(false);
    builtin_draw_path(&ctx,args,4);
    assert(lines==3);
    assert(segments[0][0]==100 && segments[0][1]==200 && segments[0][2]==120 && segments[0][3]==200);
    assert(segments[2][2]==100 && segments[2][3]==200);
    lines=0; args[3]=RValue_makeBool(true); runner.applyOffsetForPrimitives=true;
    builtin_draw_path(&ctx,args,4);
    assert(lines==3 && segments[0][0]==11 && segments[0][1]==21);
    lines=0; path.internalPointCount=0;
    builtin_draw_path(&ctx,args,4); assert(lines==0);
    // Legacy and current WAD paths must not expand a rectangle twice. TP bars
    // give the final inclusive pixel at sprite_width-1.
    RValue rectArgs[]={RValue_makeInt32(3),RValue_makeInt32(5),RValue_makeInt32(12),RValue_makeInt32(19),RValue_makeBool(false),RValue_makeInt32(0),RValue_makeInt32(0),RValue_makeInt32(0),RValue_makeBool(false)};
    float expected[]={3,5,12,19};
    for(int legacy=0;legacy<2;++legacy) {
        runner.applyOffsetForPrimitives=legacy;
        builtin_draw_rectangle(&ctx,rectArgs,5);
        assert(memcmp(rectangle,expected,sizeof(expected))==0);
        builtin_draw_rectangle_color(&ctx,rectArgs,9);
        assert(memcmp(rectangle,expected,sizeof(expected))==0);
    }
    RValue arrow[]={RValue_makeInt32(0),RValue_makeInt32(0),RValue_makeInt32(40),RValue_makeInt32(0),RValue_makeInt32(16)};
    for(int legacy=0;legacy<2;++legacy) {
        runner.applyOffsetForPrimitives=legacy;lines=triangles=0;
        builtin_draw_arrow(&ctx,arrow,5);
        assert(lines==1 && triangles==1 && segments[0][0]==legacy && segments[0][2]==40+legacy);
        assert(triangle[0]==24+legacy && fabsf(triangle[1]-(16.0f/3+legacy))<0.0001f);
        assert(triangle[2]==40+legacy && triangle[3]==legacy);
        assert(triangle[4]==24+legacy && fabsf(triangle[5]-(-16.0f/3+legacy))<0.0001f);
    }
    runner.applyOffsetForPrimitives=false;lines=triangles=0;
    arrow[2]=RValue_makeInt32(3);arrow[3]=RValue_makeInt32(4);arrow[4]=RValue_makeInt32(99);
    builtin_draw_arrow(&ctx,arrow,5);
    assert(lines==1 && triangles==1 && fabsf(triangle[0]+4.0f/3)<0.0001f && triangle[1]==1);
    arrow[2]=arrow[3]=RValue_makeInt32(0);lines=triangles=0;
    builtin_draw_arrow(&ctx,arrow,5);assert(lines==0 && triangles==0);
    // Sprite-font records grow via realloc; dirty allocator contents must not
    // become font metadata or invalid bools in battle-number fonts.
    data.font.fonts=malloc(sizeof(Font));memset(data.font.fonts,0xA5,sizeof(Font));data.font.count=0;
    int32_t indices[]={0};Sprite sprite={.tpagIndices=indices,.textureCount=1,.height=8};
    TexturePageItem tpag={.sourceWidth=6,.sourceHeight=8,.boundingWidth=6,.boundingHeight=8};
    data.sprt.sprites=&sprite;data.sprt.count=1;data.tpag.items=&tpag;data.tpag.count=1;
    ctx.dataWin=&data;uint16_t codes[]={'0'};
    RValue fontIndex=fontAddSpriteImpl(&ctx,0,codes,1,false,0);
    assert(RValue_toInt32(fontIndex)==0);
    Font* font=&data.font.fonts[0];
    assert(font->present && font->isSpriteFont && !font->hasAscender && !font->hasSDFSpread && !font->hasLineHeight);
    assert(font->ascender==0 && font->sdfSpread==0 && font->lineHeight==0);
    assert(font->glyphLUT['0']==font->glyphs);
    free(font->glyphs);free(data.font.fonts);
    args[0]=RValue_makeInt32(-1);
    builtin_draw_path(&ctx,args,4); assert(lines==0);
    puts("PASS: surface pixel ABGR/RGB packing, invalid IDs and relative/absolute closed paths");
    return 0;
}
