#undef ENABLE_VM_TRACING
#include "../src/vm_builtins.c"
#include <assert.h>

static int draws;
static void capture(Renderer* r,int32_t page,float x,float y,float ox,float oy,float sx,float sy,float angle,uint32_t color,float alpha) {
    (void)r;(void)ox;(void)oy;(void)angle;
    assert(page==0 && x==35 && y==47 && sx==2 && sy==-1);
    assert(color==0x123456 && alpha==.75f);++draws;
}
int main(void) {
    // A cubic's X handle must participate in finding its Y. Linear fallback
    // gives .2 here; the authored cubic has X=.2, Y=.5 at its middle sample.
    AnimCurvePoint points[2]={{.x=0,.value=0,.bezierX1=0,.bezierY1=0},
                            {.x=1,.value=1,.bezierX0=-.8f,.bezierY0=0}};
    AnimCurveChannel curve={.curveType=ANIMCURVE_TYPE_BEZIER,.iterations=16,.pointCount=2,.points=points};
    assert(fabsf(animcurveChannelEvaluate(&curve,.2f)-.5f)<.00001f);
    assert(animcurveChannelEvaluate(&curve,-1)==0 && animcurveChannelEvaluate(&curve,2)==1);
    points[0].bezierX1=1.f/3;points[0].bezierY1=1;
    points[1].bezierX0=-1.f/3;points[1].bezierY0=0;
    assert(fabsf(animcurveChannelEvaluate(&curve,.5f)-.875f)<.00001f);
    curve.curveType=ANIMCURVE_TYPE_LINEAR;assert(animcurveChannelEvaluate(&curve,.5f)==.5f);

    int32_t page=0;Sprite sprite={.textureCount=1,.tpagIndices=&page};
    DataWin data={.sprt={.count=1,.sprites=&sprite}};
    RendererVtable api={.drawSprite=capture};Renderer renderer={.dataWin=&data,.vtable=&api};
    Runner runner={.renderer=&renderer};VMContext vm={.runner=&runner};
    RuntimeLayer old={.id=10,.visible=true},created={.id=20,.visible=true,.xOffset=5,.yOffset=7};
    RuntimeSpriteElement crystal={.spriteIndex=0,.x=30,.y=40,.scaleX=2,.scaleY=-1};
    RuntimeLayerElement element={.id=42,.type=RuntimeLayerElementType_Sprite,.visible=true,
                                 .spriteElement=&crystal,.blend=0x123456,.alpha=.75f};
    arrput(old.elements,element);arrput(runner.runtimeLayers,old);arrput(runner.runtimeLayers,created);
    RValue move[]={RValue_makeInt32(42),RValue_makeInt32(20)};
    builtin_layer_element_move(&vm,move,2);
    assert(arrlen(runner.runtimeLayers[0].elements)==0 && arrlen(runner.runtimeLayers[1].elements)==1);
    Runner_drawLayerSprites(&runner,&runner.runtimeLayers[1]);assert(draws==1);
    builtin_layer_element_move(&vm,move,2);assert(arrlen(runner.runtimeLayers[1].elements)==1);
    move[1]=RValue_makeInt32(999);builtin_layer_element_move(&vm,move,2);
    assert(arrlen(runner.runtimeLayers[1].elements)==1);
    runner.runtimeLayers[1].elements[0].visible=false;
    Runner_drawLayerSprites(&runner,&runner.runtimeLayers[1]);assert(draws==1);
    arrfree(runner.runtimeLayers[0].elements);arrfree(runner.runtimeLayers[1].elements);arrfree(runner.runtimeLayers);
    puts("PASS: sampled Bezier X/Y handles; moved sprites draw on runtime-created layers with offsets, colour, alpha and visibility");
}
