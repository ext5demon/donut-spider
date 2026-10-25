#define SDL_MAIN_HANDLED
#include "../vendor/mojoal/mojoal.c"
#include <assert.h>
#include <math.h>

static int render(ALCcontext* ctx, ALsource* src, ALbuffer* buffer, BufferQueueItem* next, float* out, int frames) {
    BufferQueueItem item = {.buffer=buffer,.next=next};
    int bytes=frames*ctx->device->framesize;
    float* cursor=out;
    mix_source_buffer(ctx,src,&item,&cursor,&bytes);
    return frames-bytes/ctx->device->framesize;
}
static ALsource source(float pitch) {
    ALsource s={0};s.pitch=pitch;s.type=AL_STATIC;s.panning[0]=s.panning[1]=1;
    return s;
}
int main(void) {
    SDL_SetMainReady();
    assert(SDL_Init(0)==0);
    ALCdevice device={.frequency=48000,.channels=2,.framesize=8};
    ALCcontext ctx={.device=&device,.source_lock=SDL_CreateMutex()};
    float data[]={0,100,1,101,2,102,3,103,4,104,5,105};
    ALbuffer buffer={.data=data,.len=sizeof(data),.frequency=48000,.channels=2};
    float out[64]={0};
    ALsource src=source(0.5f);
    assert(render(&ctx,&src,&buffer,NULL,out,16)==12);
    for (int i=0;i<12;++i) {
        assert(fabsf(out[i*2]-fminf(i*0.5f,5))<0.0001f);
        assert(fabsf(out[i*2+1]-out[i*2]-100)<0.0001f);
    }
    memset(out,0,sizeof(out));src=source(2);
    assert(render(&ctx,&src,&buffer,NULL,out,16)==3);
    assert(out[0]==0 && out[2]==2 && out[4]==4);
    memset(out,0,sizeof(out));src=source(1);buffer.frequency=96000;
    assert(render(&ctx,&src,&buffer,NULL,out,16)==3);
    assert(out[0]==0 && out[2]==2 && out[4]==4);
    buffer.frequency=48000;
    memset(out,0,sizeof(out));src=source(0.75f);
    assert(render(&ctx,&src,&buffer,NULL,out,2)==2);
    assert(render(&ctx,&src,&buffer,NULL,out+4,2)==2);
    assert(out[0]==0 && out[2]==0.75f && out[4]==1.5f && out[6]==2.25f);
    memset(out,0,sizeof(out));src=source(1);src.looping=AL_TRUE;
    BufferQueueItem looping={.buffer=&buffer};
    assert(mix_source_buffer_queue(&ctx,&src,&looping,out,16*device.framesize));
    for(int i=0;i<16;++i) {
        assert(out[i*2]==i%6 && out[i*2+1]==100+i%6);
    }
    // Interpolate across queue boundaries and carry fractional/whole skips.
    ALbuffer a=buffer,b=buffer;a.len=6*sizeof(float);b.len=a.len;b.data=data+6;
    BufferQueueItem next={.buffer=&b};src=source(2.5f);src.type=AL_STREAMING;
    memset(out,0,sizeof(out));
    assert(render(&ctx,&src,&a,&next,out,16)==2);
    assert(out[2]==2.5f && fabs(src.resample_offset-2)<0.0001);
    assert(render(&ctx,&src,&b,NULL,out+4,16)==1 && out[4]==5);
    // Very high static-loop pitch must consume output, not loop per skipped frame.
    src=source(1e30f);src.looping=AL_TRUE;
    assert(render(&ctx,&src,&buffer,NULL,out,4)==1);
    assert(src.resample_offset>=0 && src.resample_offset<6);
    src=source(1);
    int allocations=SDL_GetNumAllocations();
    for (int i=0;i<10000;++i) source_set_pitch(&ctx,&src,(i&1)?0.8f:1.2f);
    assert(SDL_GetNumAllocations()==allocations);
    source_set_pitch(&ctx,&src,0);assert(src.pitch>0);
    source_set_pitch(&ctx,&src,-1);assert(src.pitch>0);
    source_set_pitch(&ctx,&src,NAN);assert(src.pitch>0);
    source_set_pitch(&ctx,&src,INFINITY);assert(src.pitch>0);
    ALbuffer longbuffer=buffer;longbuffer.len=48000*8;
    src=source(1);src.buffer=&longbuffer;src.resample_offset=0.75;
    alcMakeContextCurrent(&ctx);
    source_set_offset(&src,AL_SEC_OFFSET,0.5f);
    assert(src.offset==24000*8 && src.resample_offset==0);
    assert(source_get_offset(&src,AL_SEC_OFFSET)==0.5f);
    alcMakeContextCurrent(NULL);
    // Distinct mono samples must retain order in both SIMD and scalar mixers.
    // Constant signals would hide an adjacent-pair shuffle reversal.
    float mono[24];for(int i=0;i<24;++i)mono[i]=i+.25f;
    ALbuffer monoBuffer={.data=mono,.len=sizeof(mono),.frequency=48000,.channels=1};
    src=source(1);src.panning[1]=.5f;memset(out,0,sizeof(out));
    assert(render(&ctx,&src,&monoBuffer,NULL,out,24)==24);
    for(int i=0;i<24;++i){assert(out[i*2]==mono[i]);assert(out[i*2+1]==mono[i]*.5f);}
    SDL_DestroyMutex(ctx.source_lock);SDL_Quit();
    puts("PASS: pitch changes duration/rate, preserves stereo, fractional cursors and queue boundaries; no pitch allocation");
    return 0;
}
