#define SDL_MAIN_HANDLED
#include "../vendor/mojoal/mojoal.c"
#include "audio/openal/wave.h"
#include <assert.h>
#include <stdio.h>

static void exercise(float* data,int frames,int frequency) {
    ALCdevice device={.frequency=48000,.channels=2,.framesize=8};
    SourceBlock block={.used=1};SourceBlock* blocks[]={&block};
    ALCcontext ctx={.device=&device,.source_lock=SDL_CreateMutex(),.source_blocks=blocks,.num_source_blocks=1};
    ALbuffer buffer={.data=data,.len=frames*sizeof(float),.frequency=frequency,.channels=1};
    SDL_AtomicSet(&buffer.refcount,1);
    ALsource* src=&block.sources[0];
    src->allocated=AL_TRUE;src->type=AL_STATIC;src->name=1;src->pitch=1;src->buffer=&buffer;
    src->panning[0]=1;src->panning[1]=.5f;SDL_AtomicSet(&src->state,AL_INITIAL);
    alcMakeContextCurrent(&ctx);
    const ALuint name=1;float out[1600*2]={0};
    source_play(&ctx,1,&name);mix_context(&ctx,out,sizeof(out));
    float worstHardJump=0,worstSmoothJump=0;
    for(unsigned letter=0;letter<32;++letter) {
        float last[2]={out[3198],out[3199]};
        source_stop(&ctx,1);assert(SDL_AtomicGet(&src->state)==AL_STOPPED);
        source_set_pitch(&ctx,src,.9f+.15f*(letter%7)/6);
        source_play(&ctx,1,&name);assert(SDL_AtomicGet(&src->state)==AL_PLAYING);
        memset(out,0,sizeof(out));mix_context(&ctx,out,sizeof(out));
        // At the cut boundary, continuation from the old sample falls by only
        // 1/48 of its value. The new sample begins at zero and rises over 1ms.
        for(unsigned ch=0;ch<2;++ch) {
            float jump=fabsf(out[ch]-last[ch]);
            if(!(jump<=fabsf(last[ch])/48+0.00001f))fprintf(stderr,"boundary letter=%u channel=%u previous=%g first=%g jump=%g last_source=%g pan=%g,%g tail=%g,%g\n",letter,ch,last[ch],out[ch],jump,src->last_output[ch],src->panning[0],src->panning[1],ctx.stop_tail[0],ctx.stop_tail[1]);
            assert(jump<=fabsf(last[ch])/48+0.00001f);
            if(fabsf(last[ch])>worstHardJump)worstHardJump=fabsf(last[ch]);
            if(jump>worstSmoothJump)worstSmoothJump=jump;
        }
        assert(src->fadein_frames==0&&ctx.stop_tail_frames==0);
    }
    assert(worstHardJump>.0001f&&worstSmoothJump<worstHardJump/40);
    float last=out[3198];
    source_stop(&ctx,1);memset(out,0,sizeof(out));
    // Split the tail across callbacks to verify phase/remaining-frame carry.
    mix_context(&ctx,out,17*8);mix_context(&ctx,out+34,(1600-17)*8);
    assert(fabsf(out[0]-last*47/48)<.00001f);
    for(unsigned i=48*2;i<3200;++i)assert(out[i]==0);
    // Delete a playing source and free its PCM before rendering its tail.
    source_play(&ctx,1,&name);memset(out,0,sizeof(out));mix_context(&ctx,out,sizeof(out));last=out[3198];
    _alDeleteSources(1,&name);assert(!src->allocated&&src->buffer==NULL);
    free(data);buffer.data=NULL;
    memset(out,0,sizeof(out));mix_context(&ctx,out,sizeof(out));
    assert(fabsf(out[0]-last*47/48)<.00001f);
    for(unsigned i=48*2;i<3200;++i)assert(out[i]==0);
    assert(ctx.playlist==NULL&&ctx.stop_tail_frames==0);
    SourcePlayTodo* item=device.playback.source_todo_pool;
    while(item){SourcePlayTodo* next=item->next;SDL_free(item);item=next;}
    alcMakeContextCurrent(NULL);SDL_DestroyMutex(ctx.source_lock);
    printf("PASS: 32 voice cuts/retriggers, %.6f hard jump -> %.6f smoothed; stereo tail, split callbacks, immediate Stop and deletion-safe PCM release\n",worstHardJump,worstSmoothJump);
}
int main(int argc,char** argv) {
    SDL_SetMainReady();assert(SDL_Init(0)==0);
    float* signal=malloc(48000*sizeof(float));for(unsigned i=0;i<48000;++i)signal[i]=.75f;
    exercise(signal,48000,48000);
    for(int i=1;i<argc;++i) {
        FILE* f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);
        uint8_t* raw=malloc(bytes);assert(fread(raw,1,bytes,f)==(size_t)bytes);fclose(f);
        WAVFile wav;assert(WAV_ParseFileData(raw,bytes,&wav)&&wav.header.bits_per_sample==16&&wav.header.number_of_channels==1);
        unsigned frames=wav.data_length/2;signal=malloc(frames*sizeof(float));
        for(unsigned j=0;j<frames;++j){int16_t value;memcpy(&value,wav.data+j*2,2);signal[j]=value/32768.f;}
        free(raw);free(wav.data);exercise(signal,frames,wav.header.sample_rate);
    }
    SDL_Quit();return 0;
}
