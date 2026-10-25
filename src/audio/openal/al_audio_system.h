#ifndef _BS_AL_AUDIO_SYSTEM_H_
#define _BS_AL_AUDIO_SYSTEM_H_

#include "common.h"
#include "audio_system.h"
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#if defined(PLATFORM_PS3) || defined(AL_AUDIO_THREAD_TEST)
#define AL_AUDIO_THREADED
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_atomic.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_error.h>
#ifdef PLATFORM_PS3
#include <sys/thread.h>
#endif
#endif

#define MAX_SOUND_INSTANCES 128
#define SOUND_INSTANCE_ID_BASE 100000
#define MAX_AUDIO_STREAMS 32
// This is the index space that the native runner uses
#define AUDIO_STREAM_INDEX_BASE 300000

#define AL_SOUND_CACHE_ENTRIES 64
#define AL_SOUND_CACHE_BYTES (4u * 1024u * 1024u)

#define AL_STREAM_BUFFER_COUNT 4
#define AL_STREAM_BUFFER_SAMPLES 4096

struct stb_vorbis;

typedef struct {
    ALuint buffer;
    int32_t audioGroup, audioFile;
    uint32_t users;
    size_t bytes; // mojoAL stores one float per decoded sample
    uint64_t lastUse;
    float seconds;
} ALSoundBuffer;


typedef struct {
    bool active;
    int32_t soundIndex; // SOND resource that spawned this
    int32_t instanceId; // unique ID returned to GML
    ALuint alSource; // OpenAL source object
    ALSoundBuffer* cachedBuffer; // shared immutable samples, never evicted while referenced
    ALuint alBuffer; // OpenAL buffer object (only valid when streaming == false)
    float targetGain;
    float currentGain;
    float fadeTimeRemaining;
    float fadeTotalTime;
    float startGain;
    int32_t priority;

    // Streaming state (only valid when streaming == true)
    bool paused;
    bool streaming;
    bool loop;
    bool streamEnded; // decoder produced no more samples; waiting for queue to drain
    struct stb_vorbis* vorbis;
    ALuint streamBuffers[AL_STREAM_BUFFER_COUNT];
    int16_t* decodeScratch; // sized for AL_STREAM_BUFFER_SAMPLES * streamChannels shorts
    int streamChannels;
    int streamSampleRate;
    ALenum streamFormat;
    float streamLengthSeconds;
    uint64_t playedSamples; // cumulative per-channel samples that have left the queue
    uint32_t underrunCount;
} SoundInstance;

typedef struct {
    bool active;
    char* filePath; // resolved file path (owned, freed on destroy)
    float lengthSeconds; // -1 until queried or played
} AudioStreamEntry;

typedef struct {
    AudioSystem base;
#ifdef AL_AUDIO_THREADED
    SDL_mutex* serviceMutex;
#ifdef PLATFORM_PS3
    sys_ppu_thread_t serviceThread;
#else
    SDL_Thread* serviceThread;
#endif
    bool serviceThreadActive;
    SDL_atomic_t serviceStop;
#endif
    ALCdevice* alDevice;
    ALCcontext* alContext;
    SoundInstance instances[MAX_SOUND_INSTANCES];
    int32_t nextInstanceCounter;
    FileSystem* fileSystem;
    AudioStreamEntry streams[MAX_AUDIO_STREAMS];
    ALSoundBuffer soundCache[AL_SOUND_CACHE_ENTRIES];
    size_t soundCacheBytes;
    uint64_t soundCacheClock, soundCacheHits, soundCacheMisses;

} AlAudioSystem;

AlAudioSystem* AlAudioSystem_create(void);

#endif /* _BS_AL_AUDIO_SYSTEM_H_ */
