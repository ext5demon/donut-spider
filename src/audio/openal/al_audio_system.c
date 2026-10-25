// On Windows, include windows.h first so its headers are processed before stb_vorbis
// defines single-letter macros (L, C, R) that conflict with winnt.h struct field names.
#ifdef _WIN32
#include <windows.h>
#endif

#include "al_audio_system.h"
#include "stb_vorbis.c"
#include "data_win.h"
#include "utils.h"
#include "wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_ds.h"

// ===[ Helpers ]===

static bool alSourceIsPlaying(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

static bool alSourceHasStopped(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_STOPPED;
}

static bool alSourceIsLooping(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_LOOPING, &state);
    return state != AL_FALSE;
}

// Source - https://stackoverflow.com/a/7995655
// Posted by Karl
// Retrieved 2026-05-05, License - CC BY-SA 3.0
static void alGetSourceLengthSec(ALuint buffer, float* out) {
    ALint sizeInBytes;
    ALint channels;
    ALint bits;

    alGetBufferi(buffer, AL_SIZE, &sizeInBytes);
    alGetBufferi(buffer, AL_CHANNELS, &channels);
    alGetBufferi(buffer, AL_BITS, &bits);

    int lengthInSamples = sizeInBytes * 8 / (channels * bits);
    ALint frequency;

    alGetBufferi(buffer, AL_FREQUENCY, &frequency);

    *out = (float)lengthInSamples / (float)frequency;
}

// Tears down whatever AL state is attached to a slot and marks it inactive.
static void releaseInstance(SoundInstance* inst) {
    if (!inst->active)
        return;

    alSourceStop(inst->alSource);

    if (inst->streaming) {
        // Drain anything still queued so the buffer names are detachable.
        ALint queued = 0;
        alGetSourcei(inst->alSource, AL_BUFFERS_QUEUED, &queued);
        repeat(queued, i) {
            ALuint b;
            alSourceUnqueueBuffers(inst->alSource, 1, &b);
        }
        alDeleteSources(1, &inst->alSource);
        alDeleteBuffers(AL_STREAM_BUFFER_COUNT, inst->streamBuffers);
        if (inst->vorbis != nullptr) {
            stb_vorbis_close((stb_vorbis*) inst->vorbis);
            inst->vorbis = nullptr;
        }
        free(inst->decodeScratch);
        inst->decodeScratch = nullptr;
        inst->streaming = false;
    } else {
        alDeleteSources(1, &inst->alSource);
        if (inst->cachedBuffer) {
            --inst->cachedBuffer->users;
            inst->cachedBuffer = nullptr;
        } else {
            alDeleteBuffers(1, &inst->alBuffer);
        }
    }

    inst->active = false;
}

// Decode the next chunk from inst->vorbis into inst->decodeScratch and upload it to "buf".
// Wraps around on EOF if inst->loop is set.
// Returns false when no more samples are available (decoder exhausted and not looping, or read failed).
static bool streamFillBuffer(SoundInstance* inst, ALuint buf) {
    stb_vorbis* v = (stb_vorbis*) inst->vorbis;
    int samples = stb_vorbis_get_samples_short_interleaved(v, inst->streamChannels, inst->decodeScratch, AL_STREAM_BUFFER_SAMPLES * inst->streamChannels);
    if (0 >= samples) {
        if (!inst->loop) return false;
        stb_vorbis_seek_start(v);
        samples = stb_vorbis_get_samples_short_interleaved(v, inst->streamChannels, inst->decodeScratch, AL_STREAM_BUFFER_SAMPLES * inst->streamChannels);
        if (0 >= samples) return false;
    }
    alBufferData(buf, inst->streamFormat, inst->decodeScratch, samples * inst->streamChannels * (ALsizei) sizeof(int16_t), inst->streamSampleRate);
    return true;
}

static SoundInstance* findFreeSlot(AlAudioSystem* ma) {
    // First pass: find an inactive slot
    repeat(MAX_SOUND_INSTANCES, i) {
        if (!ma->instances[i].active) {
            return &ma->instances[i];
        }
    }

    // Second pass: evict the lowest-priority ended sound.
    // Streaming instances can briefly report AL_STOPPED during an underrun, so exclude them from eviction to keep music alive across SFX bursts.
    SoundInstance* best = nullptr;
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->streaming)
            continue;

        if (!inst->paused && !alSourceIsPlaying(inst->alSource)) {
            if (best == nullptr || best->priority > inst->priority) {
                best = inst;
            }
        }
    }

    if (best != nullptr) {
        releaseInstance(best);
    }

    return best;
}

static bool isStreamResource(int32_t id) {
    return id >= AUDIO_STREAM_INDEX_BASE && id < AUDIO_STREAM_INDEX_BASE + MAX_AUDIO_STREAMS;
}

static bool isSoundInstance(int32_t id) {
    return id >= SOUND_INSTANCE_ID_BASE && !isStreamResource(id);
}

static SoundInstance* findInstanceById(AlAudioSystem* ma, int32_t instanceId) {
    if (!isSoundInstance(instanceId)) return nullptr;
    int32_t slotIndex = (instanceId - SOUND_INSTANCE_ID_BASE) % MAX_SOUND_INSTANCES;
    SoundInstance* inst = &ma->instances[slotIndex];
    if (!inst->active || inst->instanceId != instanceId) return nullptr;
    return inst;
}

// Helper: resolve external audio file path from Sound entry
static char* resolveExternalPath(AlAudioSystem* ma, Sound* sound) {
    const char* file = sound->file;
    if (file == nullptr || file[0] == '\0') return nullptr;

    // If the filename has no extension, append ".ogg"
    bool hasExtension = (strchr(file, '.') != nullptr);

    char filename[512];
    if (hasExtension) {
        snprintf(filename, sizeof(filename), "%s", file);
    } else {
        snprintf(filename, sizeof(filename), "%s.ogg", file);
    }

    return ma->fileSystem->vtable->resolvePath(ma->fileSystem, filename);
}

static void maUpdate(AudioSystem* audio, float deltaTime);

#ifdef AL_AUDIO_THREADED
static int serviceAudio(void* userdata) {
    AlAudioSystem* ma = userdata;
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0)
        fprintf(stderr, "Audio: stream worker priority: %s\n", SDL_GetError());
    while (!SDL_AtomicGet(&ma->serviceStop)) {
        SDL_LockMutex(ma->serviceMutex);
        maUpdate(&ma->base, 0.0f);
        SDL_UnlockMutex(ma->serviceMutex);
        SDL_Delay(10);
    }
    return 0;
}
#ifdef PLATFORM_PS3
static void serviceAudioPS3(void* userdata) {
    serviceAudio(userdata);
    sysThreadExit(0);
}
#endif
#endif

static DataWin* loadedGroup(AudioSystem* audio, int32_t index) {
    return index >= 0 && index < arrlen(audio->audioGroups) ? audio->audioGroups[index] : nullptr;
}

// Samples are immutable once uploaded. Repeated text/graze sounds share the
// upload instead of doing filesystem I/O, endian conversion and float conversion
// for every letter. The existing service mutex protects references and LRU state.
static ALSoundBuffer* findCachedBuffer(AlAudioSystem* ma, Sound* sound) {
    for (int i = 0; i < AL_SOUND_CACHE_ENTRIES; ++i) {
        ALSoundBuffer* entry = &ma->soundCache[i];
        if (entry->buffer && entry->audioGroup == sound->audioGroup && entry->audioFile == sound->audioFile) {
            entry->lastUse = ++ma->soundCacheClock;
            ++ma->soundCacheHits;
            return entry;
        }
    }
    ++ma->soundCacheMisses;
    return nullptr;
}

static ALSoundBuffer* cacheUploadedBuffer(AlAudioSystem* ma, Sound* sound, ALuint buffer, size_t bytes, float seconds) {
    if (bytes > AL_SOUND_CACHE_BYTES) return nullptr;
    for (;;) {
        ALSoundBuffer* vacant = nullptr;
        ALSoundBuffer* oldest = nullptr;
        for (int i = 0; i < AL_SOUND_CACHE_ENTRIES; ++i) {
            ALSoundBuffer* entry = &ma->soundCache[i];
            if (!entry->buffer) { if (!vacant) vacant = entry; }
            else if (!entry->users && (!oldest || entry->lastUse < oldest->lastUse)) oldest = entry;
        }
        if (vacant && ma->soundCacheBytes <= AL_SOUND_CACHE_BYTES - bytes) {
            *vacant = (ALSoundBuffer){.buffer=buffer, .audioGroup=sound->audioGroup,
                .audioFile=sound->audioFile, .bytes=bytes, .seconds=seconds, .lastUse=++ma->soundCacheClock};
            ma->soundCacheBytes += bytes;
            return vacant;
        }
        if (!oldest) return nullptr; // all candidates are in use; keep this upload private
        alDeleteBuffers(1, &oldest->buffer);
        ma->soundCacheBytes -= oldest->bytes;
        memset(oldest, 0, sizeof(*oldest));
    }
}

// ===[ Vtable Implementations ]===

static void maInit(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    arrput(ma->base.audioGroups, dataWin);
    ma->fileSystem = fileSystem;

    ma->alDevice = alcOpenDevice(nullptr);
    ma->alContext = alcCreateContext(ma->alDevice, nullptr);
    alcMakeContextCurrent(ma->alContext);
    if (ma->alDevice == nullptr || ma->alContext == nullptr) {
        fprintf(stderr, "Audio: Failed to initialize OpenAL engine (error %d)\n", alGetError());
        return;
    }

    memset(ma->instances, 0, sizeof(ma->instances));
    ma->nextInstanceCounter = 0;

#ifdef AL_AUDIO_THREADED
    ma->serviceMutex = SDL_CreateMutex();
    if (ma->serviceMutex != nullptr) {
#ifdef PLATFORM_PS3
        // This SDK's SDL backend hardcodes a 16 KiB stack, ignoring requested
        // sizes. Vorbis uses temporary stack storage: reserve 256 KiB directly.
        int result = sysThreadCreate(&ma->serviceThread, serviceAudioPS3, ma,
                                     750, 256 * 1024, THREAD_JOINABLE, "sp-audio");
        ma->serviceThreadActive = result == 0;
        if (result != 0) fprintf(stderr, "Audio: stream worker creation failed (%d)\n", result);
#else
        ma->serviceThread = SDL_CreateThreadWithStackSize(serviceAudio, "audio-stream", 256 * 1024, ma);
        ma->serviceThreadActive = ma->serviceThread != nullptr;
#endif
    }
    fprintf(stderr, "Audio: stream worker %s\n", ma->serviceThreadActive ? "running (10ms, 256KiB stack)" : "unavailable; using frame updates");
#endif
    fprintf(stderr, "Audio: OpenAL engine initialized\n");
}

static void maDestroy(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

#ifdef AL_AUDIO_THREADED
    SDL_AtomicSet(&ma->serviceStop, 1);
    if (ma->serviceThreadActive) {
#ifdef PLATFORM_PS3
        u64 result;
        sysThreadJoin(ma->serviceThread, &result);
#else
        SDL_WaitThread(ma->serviceThread, nullptr);
#endif
    }
    if (ma->serviceMutex) SDL_DestroyMutex(ma->serviceMutex);
#endif
    // Uninit all active sound instances
    repeat(MAX_SOUND_INSTANCES, i) {
        releaseInstance(&ma->instances[i]);
    }

    for (int i = 0; i < AL_SOUND_CACHE_ENTRIES; ++i) {
        if (ma->soundCache[i].buffer) alDeleteBuffers(1, &ma->soundCache[i].buffer);
    }

    // Free stream entries
    repeat(MAX_AUDIO_STREAMS, i) {
        if (ma->streams[i].active) {
            free(ma->streams[i].filePath);
        }
    }

    // Free loaded audio groups. The main data.win is owned by the caller, so skip index 0.
    if (arrlen(ma->base.audioGroups) > 1) {
        for (int32_t i = 1; i < (int32_t) arrlen(ma->base.audioGroups); i++) {
            if (ma->base.audioGroups[i]) DataWin_free(ma->base.audioGroups[i]);
        }
    }
    arrfree(ma->base.audioGroups);

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ma->alContext);
    alcCloseDevice(ma->alDevice);
    free(ma);
}

static void maUpdate(AudioSystem* audio, float deltaTime) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (!inst->active) continue;

        // Handle gain fading (for cases where we do manual fading)
        if (deltaTime > 0.0f && inst->fadeTimeRemaining > 0.0f) {
            inst->fadeTimeRemaining -= deltaTime;
            if (0.0f >= inst->fadeTimeRemaining) {
                inst->fadeTimeRemaining = 0.0f;
                inst->currentGain = inst->targetGain;
            } else {
                float t = 1.0f - (inst->fadeTimeRemaining / inst->fadeTotalTime);
                inst->currentGain = inst->startGain + (inst->targetGain - inst->startGain) * t;
            }
            alSourcef(inst->alSource, AL_GAIN, inst->currentGain);
        }

        if (inst->streaming) {
            if (inst->paused) continue;
            // Recycle any buffers AL has finished with: count their samples toward the play position, then refill from the decoder and re-queue at the tail.
            ALint processed = 0;
            alGetSourcei(inst->alSource, AL_BUFFERS_PROCESSED, &processed);
            while (processed > 0) {
                ALuint buf;
                alSourceUnqueueBuffers(inst->alSource, 1, &buf);
                processed--;

                ALint sizeBytes = 0, bits = 0, channels = 0;
                alGetBufferi(buf, AL_SIZE, &sizeBytes);
                alGetBufferi(buf, AL_BITS, &bits);
                alGetBufferi(buf, AL_CHANNELS, &channels);
                if (bits > 0 && channels > 0) {
                    inst->playedSamples += (uint64_t) (sizeBytes * 8 / (bits * channels));
                }

                if (!inst->streamEnded) {
                    if (streamFillBuffer(inst, buf)) {
                        alSourceQueueBuffers(inst->alSource, 1, &buf);
                    } else {
                        inst->streamEnded = true;
                    }
                }
            }

            // Reap once the queue has fully drained on a non-looping track.
            ALint queued = 0;
            alGetSourcei(inst->alSource, AL_BUFFERS_QUEUED, &queued);
            if (inst->streamEnded && queued == 0) {
                releaseInstance(inst);
                continue;
            }

            // Underrun recovery: AL goes to AL_STOPPED if the queue runs dry.
            // Kick it back on as soon as we have buffers queued again.
            if (alSourceHasStopped(inst->alSource) && queued > 0) {
                ++inst->underrunCount;
                // Log the first event and powers of two; avoid flooding disk
                // when diagnosing slow frames on PS3.
                if ((inst->underrunCount & (inst->underrunCount - 1u)) == 0) {
                    fprintf(stderr, "Audio: stream underrun sound=%d instance=%d count=%u\n",
                            inst->soundIndex, inst->instanceId, inst->underrunCount);
                }
                inst->paused = false;
                alSourcePlay(inst->alSource);
            }
            continue;
        }

        // Clean up ended non-looping sounds (ma_sound_at_end avoids reaping still-loading async sounds)
        if (alSourceHasStopped(inst->alSource) && !alSourceIsLooping(inst->alSource)) {
            releaseInstance(inst);
        }
    }
}

static int32_t alEmitterSlot(AlAudioSystem* ma, int32_t id) {
    for (int i=0;i<64;++i) if (ma->emitters[i].active && ma->emitters[i].id==id) return i;
    return -1;
}

static int32_t alPlaySoundWithEmitter(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop, int32_t emitter) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    int32_t emitterSlot=alEmitterSlot(ma,emitter);
    if (emitter>=0 && emitterSlot<0) return -1;

    if (!ma->alDevice || !ma->alContext) return -1;

    // Check if this is a stream index (created by audio_create_stream)
    bool isDynamicStream = (soundIndex >= AUDIO_STREAM_INDEX_BASE);
    bool isStream = isDynamicStream;
    bool soundInAudo = false;
    Sound* sound = nullptr;
    char* streamPath = nullptr;
    char* ownedStreamPath = nullptr;

    if (isDynamicStream) {
        int32_t streamSlot = soundIndex - AUDIO_STREAM_INDEX_BASE;
        if (0 > streamSlot || streamSlot >= MAX_AUDIO_STREAMS || !ma->streams[streamSlot].active) {
            fprintf(stderr, "Audio: Invalid stream index %d\n", soundIndex);
            return -1;
        }
        streamPath = ma->streams[streamSlot].filePath;
    } else {
        DataWin* dw = ma->base.audioGroups[0]; // Audio Group 0 should always be data.win
        if (0 > soundIndex || (uint32_t) soundIndex >= dw->sond.count) {
            fprintf(stderr, "Audio: Invalid sound index %d\n", soundIndex);
            return -1;
        }
        sound = &dw->sond.sounds[soundIndex];

        bool isRegular = (sound->flags & AUDIO_ENTRY_FLAG_REGULAR) == AUDIO_ENTRY_FLAG_REGULAR;
        bool isEmbedded = (sound->flags & AUDIO_ENTRY_FLAG_IS_EMBEDDED) != 0;
        bool isCompressed = (sound->flags & AUDIO_ENTRY_FLAG_IS_COMPRESSED) != 0;
        soundInAudo = !isRegular || isEmbedded || isCompressed;
        if (!soundInAudo) {
            // External OGG assets can be full-length music. Stream every one
            // through bounded buffers; this also keeps short external effects
            // from creating long-lived decoded PCM allocations.
            ownedStreamPath = resolveExternalPath(ma, sound);
            if (ownedStreamPath == nullptr) {
                fprintf(stderr, "Audio: Could not resolve path for sound '%s'\n", sound->name);
                return -1;
            }
            streamPath = ownedStreamPath;
            isStream = true;
        }
    }

    SoundInstance* slot = findFreeSlot(ma);
    if (slot == nullptr) {
        free(ownedStreamPath);
        fprintf(stderr, "Audio: No free sound slots for sound %d\n", soundIndex);
        return -1;
    }

    int32_t slotIndex = (int32_t) (slot - ma->instances);

    slot->paused = false;
    slot->streaming = false;
    slot->cachedBuffer = nullptr;
    slot->alSource = 0;
    slot->alBuffer = 0;
    slot->vorbis = nullptr;
    slot->decodeScratch = nullptr;
    slot->streamEnded = false;
    slot->playedSamples = 0;
    slot->underrunCount = 0;

    if (isStream) {
        // Streaming path: open the decoder, queue a few small buffers, and let maUpdate() top them up.
        // This avoids the multi-hundred-millisecond hang of decoding a whole song into PCM on the main thread.
        int err = 0;
        stb_vorbis* v = stb_vorbis_open_filename(streamPath, &err, nullptr);
        if (v == nullptr) {
            fprintf(stderr, "Audio: Failed to open stream '%s' (stb_vorbis err %d)\n", streamPath, err);
            free(ownedStreamPath);
            return -1;
        }
        free(ownedStreamPath);
        stb_vorbis_info info = stb_vorbis_get_info(v);

        slot->streaming = true;
        slot->loop = loop;
        slot->vorbis = v;
        slot->streamChannels = info.channels;
        slot->streamSampleRate = (int) info.sample_rate;
        slot->streamFormat = (info.channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
        slot->streamLengthSeconds = stb_vorbis_stream_length_in_seconds(v);
        if (isDynamicStream) ma->streams[soundIndex - AUDIO_STREAM_INDEX_BASE].lengthSeconds = slot->streamLengthSeconds;
        slot->decodeScratch = (int16_t *)safeMalloc(AL_STREAM_BUFFER_SAMPLES * info.channels * sizeof(int16_t));

        alGenSources(1, &slot->alSource);
        alGenBuffers(AL_STREAM_BUFFER_COUNT, slot->streamBuffers);

        int primed = 0;
        for (int i = 0; AL_STREAM_BUFFER_COUNT > i; i++) {
            if (!streamFillBuffer(slot, slot->streamBuffers[i])) break;
            alSourceQueueBuffers(slot->alSource, 1, &slot->streamBuffers[i]);
            primed++;
        }

        if (primed == 0) {
            // Empty file or decode failure: tear everything down cleanly.
            alDeleteSources(1, &slot->alSource);
            alDeleteBuffers(AL_STREAM_BUFFER_COUNT, slot->streamBuffers);
            stb_vorbis_close(v);
            free(slot->decodeScratch);
            slot->streaming = false;
            slot->vorbis = nullptr;
            slot->decodeScratch = nullptr;
            return -1;
        }
    } else {
        if (!soundInAudo || loadedGroup(audio, sound->audioGroup) == nullptr ||
            sound->audioFile < 0 || (uint32_t)sound->audioFile >= loadedGroup(audio, sound->audioGroup)->audo.count) {
            fprintf(stderr, "Audio: Invalid audio file for sound '%s'\n", sound->name);
            return -1;
        }
        ALSoundBuffer* cached = findCachedBuffer(ma, sound);
        if (cached) {
            slot->alBuffer = cached->buffer;
        } else {
            DataWin* audioGroup = loadedGroup(audio, sound->audioGroup);
            AudioEntry* entry = &audioGroup->audo.entries[sound->audioFile];
            bool ownedAudioData = false;
            const uint8_t* audioData = DataWin_acquireAudoData(audioGroup, (uint32_t)sound->audioFile, &ownedAudioData);
            WAVFile wav;
            bool parsed = audioData && WAV_ParseFileData(audioData, entry->dataSize, &wav);
            if (ownedAudioData) free((void*)audioData);
            if (!parsed) {
                fprintf(stderr, "Audio: Invalid embedded WAV for sound '%s'\n", sound->name);
                return -1;
            }
            WAV_declickShortSample(&wav);
            ALenum format = wav.header.number_of_channels == 1
                ? (wav.header.bits_per_sample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16)
                : (wav.header.bits_per_sample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16);
            (void)alGetError();
            alGenBuffers(1, &slot->alBuffer);
            alBufferData(slot->alBuffer, format, wav.data, wav.data_length, wav.header.sample_rate);
            ALenum error = alGetError();
            if (!slot->alBuffer || error != AL_NO_ERROR) {
                if (slot->alBuffer) alDeleteBuffers(1, &slot->alBuffer);
                free(wav.data);
                fprintf(stderr, "Audio: sample upload failed sound=%s error=%x\n", sound->name, error);
                return -1;
            }
            size_t sampleCount = wav.data_length / (wav.header.bits_per_sample / 8);
            float seconds = (float)wav.data_length / wav.header.byte_rate;
            free(wav.data);
            cached = cacheUploadedBuffer(ma, sound, slot->alBuffer, sampleCount * sizeof(float), seconds);
        }
        slot->cachedBuffer = cached;
        if (cached) ++cached->users;
        alGenSources(1, &slot->alSource);
        alSourcei(slot->alSource, AL_BUFFER, slot->alBuffer);
    }

    // Apply properties
    float volume = isDynamicStream ? ma->streams[soundIndex-AUDIO_STREAM_INDEX_BASE].gain : sound->volume * AudioSystem_getGroupGain(audio, sound->audioGroup);
    if (emitterSlot>=0) volume*=ma->emitters[emitterSlot].gain;
    float pitch = isDynamicStream ? ma->streams[soundIndex-AUDIO_STREAM_INDEX_BASE].pitch : sound->pitch;
    alSourcef(slot->alSource, AL_GAIN, volume);

    if (pitch != 1.0f) {
        alSourcef(slot->alSource, AL_PITCH, pitch != 0.0f ? pitch : 1.0f);
    }
    // AL_LOOPING on a streaming source only loops the currently-playing buffer, not the whole queue,
    // so streaming looping is handled by streamFillBuffer calling stb_vorbis_seek_start when the decoder runs out.
    if (!isStream)
        alSourcei(slot->alSource, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);

    // Set up instance tracking
    slot->active = true;
    slot->soundIndex = soundIndex;
    // Encode a generation and slot; a saved handle cannot control a later
    // sound that reuses this slot. Keep the dynamic-stream resource IDs clear.
    do {
        if (ma->nextInstanceCounter > (INT32_MAX - SOUND_INSTANCE_ID_BASE - MAX_SOUND_INSTANCES) / MAX_SOUND_INSTANCES)
            ma->nextInstanceCounter = 0;
        slot->instanceId = SOUND_INSTANCE_ID_BASE + ma->nextInstanceCounter++ * MAX_SOUND_INSTANCES + slotIndex;
    } while (isStreamResource(slot->instanceId));
    slot->currentGain = volume;
    slot->targetGain = volume;
    slot->fadeTimeRemaining = 0.0f;
    slot->fadeTotalTime = 0.0f;
    slot->startGain = volume;
    slot->voiceGain = 1.0f;
    slot->voicePitch = 1.0f;
    slot->priority = priority;
    slot->emitterId = emitter;


    alSourcePlay(slot->alSource);

    return slot->instanceId;
}

static int32_t maPlaySound(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop) {
    return alPlaySoundWithEmitter(audio,soundIndex,priority,loop,-1);
}
static int32_t maPlaySoundOn(AudioSystem* audio,int32_t emitter,int32_t sound,bool loop,int32_t priority) {
    if (emitter<0) return -1;
    return alPlaySoundWithEmitter(audio,sound,priority,loop,emitter);
}

static void maStopSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        // Stop specific instance
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) releaseInstance(inst);
    } else {
        // Stop all instances of this sound resource
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                releaseInstance(inst);
            }
        }
    }
}

static void maStopAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        releaseInstance(&ma->instances[i]);
    }
}

static bool maIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst == nullptr)
            return false;

        // Streaming sources can flip to AL_STOPPED for a frame during underrun, so trust the active flag instead (cleared by maUpdate when fully drained).
        if (inst->streaming)
            return inst->active;

        return inst->paused || alSourceIsPlaying(inst->alSource);
    } else {
        // Check if any instance of this sound resource is playing
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (!inst->active || inst->soundIndex != soundOrInstance) continue;
            if (inst->streaming) return true;
            if (inst->paused || alSourceIsPlaying(inst->alSource)) return true;
        }
        return false;
    }
}

static bool maIsPaused(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst=findInstanceById(ma,soundOrInstance);
        return inst != nullptr && inst->paused;
    }
    repeat(MAX_SOUND_INSTANCES,i) {
        SoundInstance* inst=&ma->instances[i];
        if (inst->active && inst->soundIndex==soundOrInstance && inst->paused) return true;
    }
    return false;
}

static void maPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            inst->paused = true;
            alSourcePause(inst->alSource);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                inst->paused = true;
                alSourcePause(inst->alSource);
            }
        }
    }
}

static void maResumeSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr && inst->paused) {
            inst->paused = false;
            alSourcePlay(inst->alSource);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance && inst->paused) {
                inst->paused = false;
                alSourcePlay(inst->alSource);
            }
        }
    }
}

static void maPauseAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && (alSourceIsPlaying(inst->alSource) || (inst->streaming && !inst->paused))) {
            inst->paused = true;
            alSourcePause(inst->alSource);
        }
    }
}

static void maResumeAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && inst->paused) {
            inst->paused = false;
            alSourcePlay(inst->alSource);
        }
    }
}

static void alSuspend(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && (alSourceIsPlaying(inst->alSource) || (inst->streaming && !inst->paused))) {
            inst->paused = true;
            alSourcePause(inst->alSource);
        }
    }
}

static void alResume(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (!inst->active) continue;
        ALint state = 0;
        alGetSourcei(inst->alSource, AL_SOURCE_STATE, &state);
        if (state == AL_PAUSED || inst->paused) {
            inst->paused = false;
            alSourcePlay(inst->alSource);
        }
    }
}

// Resource properties persist even when no voice is playing. Voices keep their
// own multipliers so changing an asset/group does not erase a muted guitar stem.
static Sound* alSoundAsset(AlAudioSystem* ma, int32_t id) {
    DataWin* dw=ma->base.audioGroups[0];
    return id>=0 && (uint32_t)id<dw->sond.count ? &dw->sond.sounds[id] : nullptr;
}
static AudioStreamEntry* alStreamAsset(AlAudioSystem* ma, int32_t id) {
    int32_t slot=id-AUDIO_STREAM_INDEX_BASE;
    return slot>=0 && slot<MAX_AUDIO_STREAMS && ma->streams[slot].active ? &ma->streams[slot] : nullptr;
}
static float alAssetGain(AlAudioSystem* ma, int32_t id) {
    Sound* sound=alSoundAsset(ma,id);
    AudioStreamEntry* stream=alStreamAsset(ma,id);
    return sound ? sound->volume*AudioSystem_getGroupGain(&ma->base,sound->audioGroup) : (stream ? stream->gain : 1.0f);
}
static float alAssetPitch(AlAudioSystem* ma, int32_t id) {
    Sound* sound=alSoundAsset(ma,id);
    AudioStreamEntry* stream=alStreamAsset(ma,id);
    return sound ? sound->pitch : (stream ? stream->pitch : 1.0f);
}
static float alVoiceGainScale(AlAudioSystem* ma, SoundInstance* inst) {
    int slot=alEmitterSlot(ma,inst->emitterId);
    return alAssetGain(ma,inst->soundIndex)*(slot>=0?ma->emitters[slot].gain:1.0f);
}
static void maSetSoundGain(AudioSystem* audio, int32_t id, float gain, uint32_t timeMs) {
    if (!isfinite(gain) || gain<0) return;
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    if (!isSoundInstance(id)) {
        Sound* sound=alSoundAsset(ma,id);
        AudioStreamEntry* stream=alStreamAsset(ma,id);
        if (sound) sound->volume=gain;
        else if (stream) stream->gain=gain;
        else return;
        repeat(MAX_SOUND_INSTANCES,i) {
            SoundInstance* inst=&ma->instances[i];
            if (inst->active && inst->soundIndex==id) maSetSoundGain(audio,inst->instanceId,inst->voiceGain,timeMs);
        }
        return;
    }
    SoundInstance* inst=findInstanceById(ma,id);
    if (!inst) return;
    inst->voiceGain=gain;
    float effective=gain*alVoiceGainScale(ma,inst);
    inst->targetGain=effective;
    if (!timeMs) {
        inst->currentGain=effective;
        inst->fadeTimeRemaining=0;
        alSourcef(inst->alSource,AL_GAIN,effective);
    } else {
        inst->startGain=inst->currentGain;
        inst->fadeTotalTime=inst->fadeTimeRemaining=(float)timeMs/1000.0f;
    }
}
static void maSetGroupGain(AudioSystem* audio, int32_t groupIndex, float gain, uint32_t timeMs) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    AudioSystem_storeGroupGain(audio,groupIndex,gain);
    repeat(MAX_SOUND_INSTANCES,i) {
        SoundInstance* inst=&ma->instances[i];
        Sound* sound=inst->active?alSoundAsset(ma,inst->soundIndex):nullptr;
        if (sound && sound->audioGroup==groupIndex) maSetSoundGain(audio,inst->instanceId,inst->voiceGain,timeMs);
    }
}
static float maGetSoundGain(AudioSystem* audio, int32_t id) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    if (isSoundInstance(id)) {
        SoundInstance* inst=findInstanceById(ma,id);
        if (!inst) return 0;
        float scale=alVoiceGainScale(ma,inst);
        return scale>0 ? inst->currentGain/scale : inst->voiceGain;
    }
    Sound* sound=alSoundAsset(ma,id);
    AudioStreamEntry* stream=alStreamAsset(ma,id);
    return sound?sound->volume:(stream?stream->gain:0);
}
static void maSetSoundPitch(AudioSystem* audio, int32_t id, float pitch) {
    if (!isfinite(pitch)) return;
    pitch=fmaxf(1.0f/256.0f,fminf(pitch,256.0f));
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    if (!isSoundInstance(id)) {
        Sound* sound=alSoundAsset(ma,id);
        AudioStreamEntry* stream=alStreamAsset(ma,id);
        if (sound) sound->pitch=pitch;
        else if (stream) stream->pitch=pitch;
        else return;
        repeat(MAX_SOUND_INSTANCES,i) {
            SoundInstance* inst=&ma->instances[i];
            if (inst->active && inst->soundIndex==id) maSetSoundPitch(audio,inst->instanceId,inst->voicePitch);
        }
        return;
    }
    SoundInstance* inst=findInstanceById(ma,id);
    if (!inst) return;
    inst->voicePitch=pitch;
    alSourcef(inst->alSource,AL_PITCH,pitch*alAssetPitch(ma,inst->soundIndex));
}
static float maGetSoundPitch(AudioSystem* audio, int32_t id) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    if (isSoundInstance(id)) {
        SoundInstance* inst=findInstanceById(ma,id);
        return inst?inst->voicePitch:1.0f;
    }
    return alAssetPitch(ma,id);
}

static int32_t maCreateEmitter(AudioSystem* audio) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;
    for(int i=0;i<64;++i) if(!ma->emitters[i].active) {
        if(ma->nextEmitterId==INT32_MAX) return -1;
        ma->emitters[i].active=true;ma->emitters[i].id=ma->nextEmitterId++;
        ma->emitters[i].gain=1;return ma->emitters[i].id;
    }
    return -1;
}
static bool maEmitterExists(AudioSystem* audio,int32_t emitter) { return alEmitterSlot((AlAudioSystem*)audio,emitter)>=0; }
static void maFreeEmitter(AudioSystem* audio,int32_t emitter) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;int slot=alEmitterSlot(ma,emitter);
    if(slot<0)return;
    repeat(MAX_SOUND_INSTANCES,i) if(ma->instances[i].active&&ma->instances[i].emitterId==emitter)releaseInstance(&ma->instances[i]);
    ma->emitters[slot].active=false;
}
static void maSetEmitterGain(AudioSystem* audio,int32_t emitter,float gain,uint32_t timeMs) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;int slot=alEmitterSlot(ma,emitter);
    if(slot<0||!isfinite(gain)||gain<0)return;
    ma->emitters[slot].gain=gain;
    repeat(MAX_SOUND_INSTANCES,i) {
        SoundInstance* inst=&ma->instances[i];
        if(inst->active&&inst->emitterId==emitter)maSetSoundGain(audio,inst->instanceId,inst->voiceGain,timeMs);
    }
}

// AL_SAMPLE_OFFSET is relative to the current queue, including processed
// buffers still in it. Add only samples already unqueued by the worker.
static float streamCursorSeconds(SoundInstance* inst) {
    if (0 >= inst->streamSampleRate)
        return 0.0f;
    
    ALint sampleOffset = 0;
    alGetSourcei(inst->alSource, AL_SAMPLE_OFFSET, &sampleOffset);
    uint64_t total = inst->playedSamples + (uint64_t) sampleOffset;
    return (float) total / (float) inst->streamSampleRate;
}

static float maGetTrackPosition(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            if (inst->streaming) return streamCursorSeconds(inst);
            float cursor;
            alGetSourcef(inst->alSource, AL_SEC_OFFSET, &cursor);
            return cursor;
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                if (inst->streaming) return streamCursorSeconds(inst);
                float cursor;
                alGetSourcef(inst->alSource, AL_SEC_OFFSET, &cursor);
                return cursor;
            }
        }
    }
    return 0.0f;
}

static void setInstancePosition(SoundInstance* inst, float seconds) {
    if (!isfinite(seconds) || seconds < 0.0f) return;
    if (!inst->streaming) {
        alSourcef(inst->alSource, AL_SEC_OFFSET, seconds);
        return;
    }
    stb_vorbis* decoder = inst->vorbis;
    unsigned int length = stb_vorbis_stream_length_in_samples(decoder);
    double requested = (double) seconds * inst->streamSampleRate;
    if (inst->loop && length) requested = fmod(requested, length);
    unsigned int sample = requested >= length ? length : (unsigned int) requested;
    if (!stb_vorbis_seek(decoder, sample)) {
        fprintf(stderr, "Audio: seek failed sound=%d seconds=%g\n", inst->soundIndex, seconds);
        return;
    }
    alSourceStop(inst->alSource);
    ALint queued = 0;
    alGetSourcei(inst->alSource, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint buffer;
        alSourceUnqueueBuffers(inst->alSource, 1, &buffer);
    }
    inst->playedSamples = sample;
    inst->streamEnded = false;
    int primed = 0;
    for (int i = 0; i < AL_STREAM_BUFFER_COUNT; ++i) {
        if (!streamFillBuffer(inst, inst->streamBuffers[i])) {
            inst->streamEnded = true;
            break;
        }
        alSourceQueueBuffers(inst->alSource, 1, &inst->streamBuffers[i]);
        ++primed;
    }
    if (primed && !inst->paused) alSourcePlay(inst->alSource);
}

static void maSetTrackPosition(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (isSoundInstance(soundOrInstance)) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            setInstancePosition(inst, positionSeconds);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
            setInstancePosition(inst, positionSeconds);
            }
        }
    }
}

// Total length of a loaded sound. Works on both SOND index and active instance ids.
static float maGetSoundLength(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (isStreamResource(soundOrInstance)) {
        AudioStreamEntry* stream = &ma->streams[soundOrInstance - AUDIO_STREAM_INDEX_BASE];
        if (!stream->active) return 0.0f;
        if (stream->lengthSeconds < 0.0f) {
            int error = 0;
            stb_vorbis* decoder = stb_vorbis_open_filename(stream->filePath, &error, nullptr);
            if (!decoder) return 0.0f;
            stream->lengthSeconds = stb_vorbis_stream_length_in_seconds(decoder);
            stb_vorbis_close(decoder);
        }
        return stream->lengthSeconds;
    }


    SoundInstance* match = nullptr;
    if (isSoundInstance(soundOrInstance)) {
        match = findInstanceById(ma, soundOrInstance);
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                match = inst;
                break;
            }
        }
    }
    if (match != nullptr) {
        if (match->streaming) return match->streamLengthSeconds;
        float seconds = 0.0f;
        alGetSourceLengthSec(match->alBuffer, &seconds);
        return seconds;
    }

    // No active instance: GMS audio_sound_length(soundIndex) must still return the asset's duration.
    if (isSoundInstance(soundOrInstance) || soundOrInstance >= AUDIO_STREAM_INDEX_BASE)
        return 0.0f;

    DataWin* dw = ma->base.audioGroups[0];
    if (dw == nullptr || 0 > soundOrInstance || (uint32_t) soundOrInstance >= dw->sond.count)
        return 0.0f;

    Sound* sound = &dw->sond.sounds[soundOrInstance];

    bool isRegular = (sound->flags & AUDIO_ENTRY_FLAG_REGULAR) == AUDIO_ENTRY_FLAG_REGULAR;
    bool isEmbedded = (sound->flags & AUDIO_ENTRY_FLAG_IS_EMBEDDED) != 0;
    bool isCompressed = (sound->flags & AUDIO_ENTRY_FLAG_IS_COMPRESSED) != 0;
    bool inAudo = !isRegular || isEmbedded || isCompressed;
    if (inAudo) {
        ALSoundBuffer* cached = findCachedBuffer(ma, sound);
        if (cached) return cached->seconds;
        if (loadedGroup(audio, sound->audioGroup) == nullptr || 0 > sound->audioFile || (uint32_t) sound->audioFile >= loadedGroup(audio, sound->audioGroup)->audo.count) return 0.0f;
        DataWin* audioGroup = ma->base.audioGroups[sound->audioGroup];
        AudioEntry* entry = &audioGroup->audo.entries[sound->audioFile];
        bool ownedAudioData = false;
        const uint8_t* audioData = DataWin_acquireAudoData(audioGroup, (uint32_t) sound->audioFile, &ownedAudioData);
        if (audioData == nullptr || entry->dataSize < 44) {
            if (ownedAudioData) free((void*) audioData);
            return 0.0f;
        }
        WAVFile wav;
        bool parsedWav = WAV_ParseFileData(audioData, entry->dataSize, &wav);
        if (ownedAudioData) free((void*) audioData);
        if (!parsedWav) return 0.0f;
        float seconds = 0.0f;
        if (wav.header.byte_rate > 0) seconds = (float) wav.header.data_size / (float) wav.header.byte_rate;
        if (wav.data != nullptr) free(wav.data);
        return seconds;
    }

    char* path = resolveExternalPath(ma, sound);
    if (path == nullptr) return 0.0f;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &err, nullptr);
    free(path);
    if (v == nullptr) return 0.0f;
    float seconds = stb_vorbis_stream_length_in_seconds(v);
    stb_vorbis_close(v);
    return seconds;
}

static void maSetMasterGainForListener(AudioSystem* audio, float gain, int32_t id) {
    (void)audio;
    (void)id;
    alListenerf(AL_GAIN, gain);
}

static void maSetMasterGain(AudioSystem* audio, float gain) {
    (void)audio;
    alListenerf(AL_GAIN, gain);
}

static void maSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {
    // miniaudio handles channel management internally, this is a no-op
}

static void maGroupLoad(AudioSystem* audio, int32_t groupIndex) {
    if (groupIndex > 0 && groupIndex < AUDIO_SYSTEM_MAX_GROUPS && loadedGroup(audio, groupIndex) == nullptr) {
        int sz = snprintf(nullptr, 0, "audiogroup%d.dat", groupIndex);
        char *buf = (char *)safeMalloc(sz + 1);
        snprintf(buf, sz + 1, "audiogroup%d.dat", groupIndex);

        // The original runner does not care if the file doesn't exist (this may happen if someone uses "audio_group_load" on a non-existent group)
        FileSystem* fileSystem = ((AlAudioSystem*)audio)->fileSystem;
        char* resolvedPath = fileSystem->vtable->resolvePath(fileSystem, buf);
        if (resolvedPath == nullptr || !fileSystem->vtable->fileExists(fileSystem, resolvedPath)) {
            fprintf(stderr, "Audio: Wanted to load Audio Group %d, but Audio Group %d does not exist!\n", groupIndex, groupIndex);
            free(resolvedPath);
            free(buf);
            return;
        }

        DataWinParserOptions options = {0};
        options.parseAudo = true;
#ifdef PLATFORM_PS3
        options.lazyLoadAudio = true;
#endif
        DataWin *audioGroup = DataWin_parse(resolvedPath, options);
        if (audioGroup != nullptr) {
            int oldCount = (int) arrlen(audio->audioGroups);
            if (oldCount <= groupIndex) {
                arrsetlen(audio->audioGroups, groupIndex + 1);
                memset(audio->audioGroups + oldCount, 0, (groupIndex + 1 - oldCount) * sizeof(DataWin*));
            }
            audio->audioGroups[groupIndex] = audioGroup;
        }
        free(resolvedPath);
        free(buf);
    }
}

static bool maGroupIsLoaded(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) {
    return loadedGroup(audio, groupIndex) != nullptr;
}

// ===[ Audio Streams ]===

static int32_t maCreateStream(AudioSystem* audio, const char* filename) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    // Find a free stream slot
    int32_t freeSlot = -1;
    repeat(MAX_AUDIO_STREAMS, i) {
        if (!ma->streams[i].active) {
            freeSlot = (int32_t) i;
            break;
        }
    }

    if (0 > freeSlot) {
        fprintf(stderr, "Audio: No free stream slots for '%s'\n", filename);
        return -1;
    }

    char* resolved = ma->fileSystem->vtable->resolvePath(ma->fileSystem, filename);
    if (resolved == nullptr) {
        fprintf(stderr, "Audio: Could not resolve path for stream '%s'\n", filename);
        return -1;
    }

    ma->streams[freeSlot].active = true;
    ma->streams[freeSlot].filePath = resolved;
    ma->streams[freeSlot].lengthSeconds = -1.0f;
    ma->streams[freeSlot].gain = ma->streams[freeSlot].pitch = 1.0f;

    int32_t streamIndex = AUDIO_STREAM_INDEX_BASE + freeSlot;
    fprintf(stderr, "Audio: Created stream %d for '%s' -> '%s'\n", streamIndex, filename, resolved);
    return streamIndex;
}

static bool maDestroyStream(AudioSystem* audio, int32_t streamIndex) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    int32_t slotIndex = streamIndex - AUDIO_STREAM_INDEX_BASE;
    if (0 > slotIndex || slotIndex >= MAX_AUDIO_STREAMS) {
        fprintf(stderr, "Audio: Invalid stream index %d for destroy\n", streamIndex);
        return false;
    }

    AudioStreamEntry* entry = &ma->streams[slotIndex];
    if (!entry->active) return false;

    // Stop all sound instances that were playing this stream
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && inst->soundIndex == streamIndex) {
            releaseInstance(inst);
        }
    }

    free(entry->filePath);
    entry->filePath = nullptr;
    entry->active = false;
    fprintf(stderr, "Audio: Destroyed stream %d\n", streamIndex);
    return true;
}

// ===[ Vtable ]===

static AudioSystemVtable AlAudioSystemVtable;

// ===[ Lifecycle ]===

#ifdef AL_AUDIO_THREADED
// Calls between backend functions use the raw implementations; only the public
// vtable takes the mutex, so nested gain/stop operations cannot deadlock.
static void maUpdateLocked(AudioSystem* audio, float deltaTime) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maUpdate(audio, deltaTime);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static int32_t maPlaySoundLocked(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    int32_t result = maPlaySound(audio, soundIndex, priority, loop);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static int32_t maPlaySoundOnLocked(AudioSystem* audio,int32_t emitter,int32_t sound,bool loop,int32_t priority) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;if(ma->serviceMutex)SDL_LockMutex(ma->serviceMutex);
    int32_t result=maPlaySoundOn(audio,emitter,sound,loop,priority);
    if(ma->serviceMutex)SDL_UnlockMutex(ma->serviceMutex);return result;
}
static int32_t maCreateEmitterLocked(AudioSystem* audio) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;if(ma->serviceMutex)SDL_LockMutex(ma->serviceMutex);
    int32_t result=maCreateEmitter(audio);if(ma->serviceMutex)SDL_UnlockMutex(ma->serviceMutex);return result;
}
static bool maEmitterExistsLocked(AudioSystem* audio,int32_t emitter) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;if(ma->serviceMutex)SDL_LockMutex(ma->serviceMutex);
    bool result=maEmitterExists(audio,emitter);if(ma->serviceMutex)SDL_UnlockMutex(ma->serviceMutex);return result;
}
static void maFreeEmitterLocked(AudioSystem* audio,int32_t emitter) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;if(ma->serviceMutex)SDL_LockMutex(ma->serviceMutex);
    maFreeEmitter(audio,emitter);if(ma->serviceMutex)SDL_UnlockMutex(ma->serviceMutex);
}
static void maSetEmitterGainLocked(AudioSystem* audio,int32_t emitter,float gain,uint32_t timeMs) {
    AlAudioSystem* ma=(AlAudioSystem*)audio;if(ma->serviceMutex)SDL_LockMutex(ma->serviceMutex);
    maSetEmitterGain(audio,emitter,gain,timeMs);if(ma->serviceMutex)SDL_UnlockMutex(ma->serviceMutex);
}
static void maStopSoundLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maStopSound(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void maStopAllLocked(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maStopAll(audio);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static bool maIsPlayingLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    bool result = maIsPlaying(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static bool maIsPausedLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    bool result = maIsPaused(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static void maPauseSoundLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maPauseSound(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void maResumeSoundLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maResumeSound(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void maPauseAllLocked(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maPauseAll(audio);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void maResumeAllLocked(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maResumeAll(audio);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void alSuspendLocked(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    alSuspend(audio);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void alResumeLocked(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    alResume(audio);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static void maSetSoundGainLocked(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maSetSoundGain(audio, soundOrInstance, gain, timeMs);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static float maGetSoundGainLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    float result = maGetSoundGain(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static void maSetSoundPitchLocked(AudioSystem* audio, int32_t soundOrInstance, float pitch) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maSetSoundPitch(audio, soundOrInstance, pitch);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static float maGetSoundPitchLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    float result = maGetSoundPitch(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static float maGetTrackPositionLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    float result = maGetTrackPosition(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static void maSetTrackPositionLocked(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maSetTrackPosition(audio, soundOrInstance, positionSeconds);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static float maGetSoundLengthLocked(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    float result = maGetSoundLength(audio, soundOrInstance);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
static void maSetGroupGainLocked(AudioSystem* audio, int32_t groupIndex, float gain, uint32_t timeMs) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    maSetGroupGain(audio, groupIndex, gain, timeMs);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
}
static bool maDestroyStreamLocked(AudioSystem* audio, int32_t streamIndex) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    if (ma->serviceMutex) SDL_LockMutex(ma->serviceMutex);
    bool result = maDestroyStream(audio, streamIndex);
    if (ma->serviceMutex) SDL_UnlockMutex(ma->serviceMutex);
    return result;
}
#define AUDIO_ENTRY(name) name##Locked
#else
#define AUDIO_ENTRY(name) name
#endif

AlAudioSystem* AlAudioSystem_create(void) {
    AlAudioSystem* ma = (AlAudioSystem *)safeCalloc(1, sizeof(AlAudioSystem));
    AlAudioSystemVtable.init = maInit;
    AlAudioSystemVtable.destroy = maDestroy;
    AlAudioSystemVtable.update = AUDIO_ENTRY(maUpdate);
    AlAudioSystemVtable.playSound = AUDIO_ENTRY(maPlaySound);
    AlAudioSystemVtable.playSoundOn = AUDIO_ENTRY(maPlaySoundOn);
    AlAudioSystemVtable.createEmitter = AUDIO_ENTRY(maCreateEmitter);
    AlAudioSystemVtable.emitterExists = AUDIO_ENTRY(maEmitterExists);
    AlAudioSystemVtable.freeEmitter = AUDIO_ENTRY(maFreeEmitter);
    AlAudioSystemVtable.setEmitterGain = AUDIO_ENTRY(maSetEmitterGain);
    AlAudioSystemVtable.stopSound = AUDIO_ENTRY(maStopSound);
    AlAudioSystemVtable.stopAll = AUDIO_ENTRY(maStopAll);
    AlAudioSystemVtable.isPlaying = AUDIO_ENTRY(maIsPlaying);
    AlAudioSystemVtable.isPaused = AUDIO_ENTRY(maIsPaused);
    AlAudioSystemVtable.pauseSound = AUDIO_ENTRY(maPauseSound);
    AlAudioSystemVtable.resumeSound = AUDIO_ENTRY(maResumeSound);
    AlAudioSystemVtable.pauseAll = AUDIO_ENTRY(maPauseAll);
    AlAudioSystemVtable.resumeAll = AUDIO_ENTRY(maResumeAll);
    AlAudioSystemVtable.suspend = AUDIO_ENTRY(alSuspend);
    AlAudioSystemVtable.resume = AUDIO_ENTRY(alResume);
    AlAudioSystemVtable.setSoundGain = AUDIO_ENTRY(maSetSoundGain);
    AlAudioSystemVtable.getSoundGain = AUDIO_ENTRY(maGetSoundGain);
    AlAudioSystemVtable.setSoundPitch = AUDIO_ENTRY(maSetSoundPitch);
    AlAudioSystemVtable.getSoundPitch = AUDIO_ENTRY(maGetSoundPitch);
    AlAudioSystemVtable.getTrackPosition = AUDIO_ENTRY(maGetTrackPosition);
    AlAudioSystemVtable.setTrackPosition = AUDIO_ENTRY(maSetTrackPosition);
    AlAudioSystemVtable.getSoundLength = AUDIO_ENTRY(maGetSoundLength);
    AlAudioSystemVtable.setMasterGain = maSetMasterGain;
    AlAudioSystemVtable.setMasterGainForListener = maSetMasterGainForListener;
    AlAudioSystemVtable.setChannelCount = maSetChannelCount;
    AlAudioSystemVtable.groupLoad = maGroupLoad;
    AlAudioSystemVtable.groupIsLoaded = maGroupIsLoaded;
    AlAudioSystemVtable.setGroupGain = AUDIO_ENTRY(maSetGroupGain);
    AlAudioSystemVtable.createStream = maCreateStream;
    AlAudioSystemVtable.destroyStream = AUDIO_ENTRY(maDestroyStream);
    ma->base.vtable = &AlAudioSystemVtable;
    return ma;
}
