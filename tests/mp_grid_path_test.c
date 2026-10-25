#undef ENABLE_VM_TRACING
#include "../src/vm_builtins.c"
#include <assert.h>

static void route(VMContext* ctx,int grid,int path,double x1,double y1,double x2,double y2,bool diagonal) {
    RValue args[]={RValue_makeInt32(grid),RValue_makeInt32(path),RValue_makeReal(x1),RValue_makeReal(y1),RValue_makeReal(x2),RValue_makeReal(y2),RValue_makeBool(diagonal)};
    assert(RValue_toBool(builtin_mp_grid_path(ctx,args,7)));
    GamePath* p=&ctx->runner->dataWin->path.paths[path];
    assert(!p->isClosed && !p->isSmooth);
    PathPositionResult first=GamePath_getPosition(p,0),last=GamePath_getPosition(p,1);
    assert(first.x==x1 && first.y==y1 && last.x==x2 && last.y==y2);
    assert(p->internalPointCount>=2);
}

int main(void) {
    DataWin data={0};Runner runner={.dataWin=&data};VMContext ctx={.runner=&runner};
    RValue gridArgs[]={RValue_makeInt32(128),RValue_makeInt32(64),RValue_makeInt32(12),RValue_makeInt32(8),RValue_makeInt32(32),RValue_makeInt32(32)};
    int grid=RValue_toInt32(builtin_mp_grid_create(&ctx,gridArgs,6));
    int susie=RValue_toInt32(builtin_path_add(&ctx,NULL,0));
    int ralsei=RValue_toInt32(builtin_path_add(&ctx,NULL,0));
    // New paths default to closed. Grid routes must still stop at the goal;
    // otherwise a relative NPC route returns to its start and stalls the scene.
    assert(data.path.paths[susie].isClosed && data.path.paths[ralsei].isClosed);
    route(&ctx,grid,susie,320,144,352,200,true);
    route(&ctx,grid,ralsei,288,144,288,200,true);
    PathPositionResult s0=GamePath_getPosition(&data.path.paths[susie],0),s1=GamePath_getPosition(&data.path.paths[susie],1);
    assert(304+s1.x-s0.x==336 && 128+s1.y-s0.y==184);
    PathPositionResult r0=GamePath_getPosition(&data.path.paths[ralsei],0),r1=GamePath_getPosition(&data.path.paths[ralsei],1);
    assert(272+r1.x-r0.x==272 && 128+r1.y-r0.y==184);
    // Reusing a formerly curved/closed path also replaces its interpolation.
    data.path.paths[susie].isSmooth=true;data.path.paths[susie].isClosed=true;
    route(&ctx,grid,susie,144,80,496,272,false);
    route(&ctx,grid,susie,145,81,149,85,true); // same-cell nonzero route
    assert(data.path.paths[susie].pointCount==2 && data.path.paths[susie].length>0);
    route(&ctx,grid,susie,145,81,145,81,false); // identical endpoints
    assert(data.path.paths[susie].pointCount==2 && data.path.paths[susie].length==0);
    for(uint32_t i=0;i<data.path.count;++i){free(data.path.paths[i].points);free(data.path.paths[i].internalPoints);}
    free(data.path.paths);
    for(ptrdiff_t i=0;i<arrlen(runner.mpGridPool);++i)free(runner.mpGridPool[i].cells);
    arrfree(runner.mpGridPool);
    puts("PASS: grid routes replace closed/curved paths and reach exact relative NPC destinations");
}
