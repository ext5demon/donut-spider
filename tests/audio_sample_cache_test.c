#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "../src/audio/openal/al_audio_system.c"
#include <assert.h>

static unsigned char* wavData;
static size_t wavSize;
static int reads;
static void put16(unsigned char* p,unsigned n) { p[0]=n;p[1]=n>>8; }
static void put32(unsigned char* p,unsigned n) { put16(p,n);put16(p+2,n>>16); }
DataWin* DataWin_parse(const char* path,DataWinParserOptions options) { (void)path;(void)options;return NULL; }
void DataWin_free(DataWin* dw) { (void)dw; }
const uint8_t* DataWin_acquireAudoData(DataWin* dw,uint32_t index,bool* owned) {
    (void)dw;(void)index;++reads;*owned=false;return wavData;
}

int main(void) {
    SDL_SetMainReady();assert(SDL_Init(SDL_INIT_AUDIO)==0);
    const int count=80,pcmBytes=302400;
    wavSize=44+pcmBytes;wavData=calloc(1,wavSize);assert(wavData);
    memcpy(wavData,"RIFF",4);put32(wavData+4,wavSize-8);memcpy(wavData+8,"WAVEfmt ",8);
    put32(wavData+16,16);put16(wavData+20,1);put16(wavData+22,2);
    put32(wavData+24,44100);put32(wavData+28,176400);put16(wavData+32,4);put16(wavData+34,16);
    memcpy(wavData+36,"data",4);put32(wavData+40,pcmBytes);
    DataWin data={0};Sound sounds[80]={0};AudioEntry entries[80]={0};
    for (int i=0;i<count;++i) {
        sounds[i]=(Sound){.name="narrator",.flags=101,.volume=0.5f,.pitch=1,.audioGroup=0,.audioFile=i};
        entries[i].dataSize=wavSize;
    }
    data.sond.count=count;data.sond.sounds=sounds;data.audo.count=count;data.audo.entries=entries;
    AlAudioSystem* ma=AlAudioSystem_create();AudioSystem* audio=&ma->base;AudioSystemVtable* v=audio->vtable;
    v->init(audio,&data,NULL);
    int first=v->playSound(audio,0,50,true),second=v->playSound(audio,0,50,true);
    assert(first>=SOUND_INSTANCE_ID_BASE && second!=first && reads==1);
    SoundInstance* held=findInstanceById(ma,second);ALSoundBuffer* shared=held->cachedBuffer;
    assert(shared && shared->users==2 && shared->buffer==findInstanceById(ma,first)->alBuffer);
    v->setSoundGain(audio,first,.25f,0);
    v->setSoundGain(audio,0,.8f,0);
    v->setGroupGain(audio,0,.5f,0);
    float applied=0;alGetSourcef(held->alSource,AL_GAIN,&applied);assert(fabsf(applied-.4f)<.0001f);
    alGetSourcef(findInstanceById(ma,first)->alSource,AL_GAIN,&applied);assert(fabsf(applied-.1f)<.0001f);
    assert(fabsf(v->getSoundGain(audio,first)-.25f)<.0001f && fabsf(v->getSoundGain(audio,0)-.8f)<.0001f);
    v->pauseSound(audio,second);assert(v->isPlaying(audio,second)&&v->isPaused(audio,second)&&v->isPaused(audio,0));
    v->resumeSound(audio,second);assert(!v->isPaused(audio,second)&&!v->isPaused(audio,-1));
    v->setSoundGain(audio,0,.5f,0);v->setGroupGain(audio,0,1,0);
    int emitter=v->createEmitter(audio);assert(emitter>=0&&v->emitterExists(audio,emitter));
    v->setEmitterGain(audio,emitter,.5f,0);
    int crowd=v->playSoundOn(audio,emitter,0,true,1);assert(crowd>=SOUND_INSTANCE_ID_BASE);
    v->setSoundGain(audio,crowd,.5f,0);
    alGetSourcef(findInstanceById(ma,crowd)->alSource,AL_GAIN,&applied);assert(fabsf(applied-.125f)<.0001f);
    v->setEmitterGain(audio,emitter,.25f,0);
    alGetSourcef(findInstanceById(ma,crowd)->alSource,AL_GAIN,&applied);assert(fabsf(applied-.0625f)<.0001f);
    v->freeEmitter(audio,emitter);assert(!v->emitterExists(audio,emitter)&&!v->isPlaying(audio,crowd));
    assert(v->playSoundOn(audio,emitter,0,true,1)==-1);
    assert(fabsf(v->getSoundLength(audio,0)-(float)pcmBytes/176400)<0.001f && reads==1);
    v->stopSound(audio,first);assert(shared->users==1 && v->isPlaying(audio,second));
    // Stress eviction with a live reference: never evict a buffer still playing.
    for (int i=1;i<count;++i) {
        int handle=v->playSound(audio,i,50,false);assert(handle>=SOUND_INSTANCE_ID_BASE);
        v->stopSound(audio,handle);
        assert(ma->soundCacheBytes<=AL_SOUND_CACHE_BYTES);
        assert(held->cachedBuffer==shared && shared->users==1);
    }
    assert(reads==count && ma->soundCacheHits>=1);
    assert(alGetError()==AL_NO_ERROR);
    int reused=v->playSound(audio,0,50,false);assert(reads==count);
    v->setSoundPitch(audio,reused,0.75f);v->stopSound(audio,reused);
    v->stopSound(audio,second);assert(shared->users==0);
    // Repeated short pitched effects previously leaked their FFT state when
    // each source was deleted. Exercise the public source lifetime under ASan.
    // Suspending processing also leaves pending play requests for teardown.
    alcSuspendContext(ma->alContext);
    for(int i=0;i<1000;++i) {
        int handle=v->playSound(audio,0,50,false);
        assert(handle>=SOUND_INSTANCE_ID_BASE);
        v->setSoundPitch(audio,handle,(i&1)?0.8f:1.2f);
        v->stopSound(audio,handle);
    }
    assert(reads==count && shared->users==0);
    // Malformed PCM must not divide by zero or publish a cache entry.
    put16(wavData+34,0);sounds[79].audioFile=100;data.audo.count=101;
    AudioEntry extended[101]={0};extended[100].dataSize=wavSize;data.audo.entries=extended;
    assert(v->playSound(audio,79,50,false)==-1);
    v->destroy(audio);free(wavData);SDL_Quit();
    puts("PASS: replay reads/uploads once, shared lifetime, independent handles, bounded LRU eviction and invalid PCM");
    return 0;
}
