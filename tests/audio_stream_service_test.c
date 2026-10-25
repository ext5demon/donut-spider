#include "../src/audio/openal/al_audio_system.c"
#include <assert.h>

// The test uses real SDL/mojoAL mixing and Vorbis decoding; only game-bundle
// lookup is synthetic so group-loading order can be exercised without a WAD.
static int parses, frees;
DataWin* DataWin_parse(const char* path, DataWinParserOptions options) {
    (void)path; (void)options; ++parses;
    return calloc(1, sizeof(DataWin));
}
void DataWin_free(DataWin* data) { ++frees; free(data); }
const uint8_t* DataWin_acquireAudoData(DataWin* data, uint32_t index, bool* owned) {
    (void)data; (void)index; *owned = false; return NULL;
}
static char* resolve(FileSystem* fs, const char* path) { (void)fs; return safeStrdup(path); }
static bool exists(FileSystem* fs, const char* path) { (void)fs; (void)path; return true; }

int main(int argc, char** argv) {
    assert(argc == 2);
    DataWin data = {0};
    FileSystemVtable files = {.resolvePath = resolve, .fileExists = exists};
    FileSystem fs = {.vtable = &files};
    AlAudioSystem* ma = AlAudioSystem_create();
    AudioSystem* audio = &ma->base;
    AudioSystemVtable* v = audio->vtable;
    v->init(audio, &data, &fs);
    assert(ma->serviceThread);
    v->groupLoad(audio, 3); v->groupLoad(audio, 1); v->groupLoad(audio, 3);
    assert(parses == 2 && v->groupIsLoaded(audio, 3) && v->groupIsLoaded(audio, 1));
    assert(!v->groupIsLoaded(audio, 2) && !v->groupIsLoaded(audio, -1));
    int stream = v->createStream(audio, argv[1]);
    assert(fabsf(v->getSoundLength(audio, stream) - 3.0f) < 0.01f);
    int sound = v->playSound(audio, stream, 0, true);
    assert(sound >= SOUND_INSTANCE_ID_BASE);
    // Deliberately do NOT run game-frame update: the worker must keep playing
    // beyond the four-buffer queue's ~0.37 seconds at 44.1 kHz.
    SDL_Delay(1200);
    float pos = v->getTrackPosition(audio, sound);
    SDL_LockMutex(ma->serviceMutex);
    SoundInstance* diag = findInstanceById(ma, sound);
    ALint state = 0, queued = 0, processed = 0, offset = 0;
    alGetSourcei(diag->alSource, AL_SOURCE_STATE, &state);
    alGetSourcei(diag->alSource, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(diag->alSource, AL_BUFFERS_PROCESSED, &processed);
    alGetSourcei(diag->alSource, AL_SAMPLE_OFFSET, &offset);
    fprintf(stderr, "TEST cursor=%f played=%llu state=%x queued=%d processed=%d offset=%d rate=%d error=%x\n", pos, (unsigned long long) diag->playedSamples, state, queued, processed, offset, diag->streamSampleRate, alGetError());
    SDL_UnlockMutex(ma->serviceMutex);
    assert(pos > 0.8f && pos < 1.6f);
    SDL_LockMutex(ma->serviceMutex);
    assert(findInstanceById(ma, sound)->underrunCount == 0);
    SDL_UnlockMutex(ma->serviceMutex);
    v->pauseSound(audio, sound);
    assert(v->isPlaying(audio,sound)&&v->isPaused(audio,sound)&&v->isPaused(audio,stream));
    float paused = v->getTrackPosition(audio, sound);
    SDL_Delay(150);
    assert(fabsf(v->getTrackPosition(audio, sound) - paused) < 0.04f);
    v->setTrackPosition(audio, sound, 2.1f);
    assert(fabsf(v->getTrackPosition(audio, sound) - 2.1f) < 0.04f);
    SDL_Delay(100);
    assert(fabsf(v->getTrackPosition(audio, sound) - 2.1f) < 0.04f);
    v->resumeSound(audio, sound);
    assert(!v->isPaused(audio,sound));
    SDL_Delay(200);
    assert(v->getTrackPosition(audio, sound) > 2.2f);
    float running=v->getTrackPosition(audio,sound);
    for(int i=0;i<5;++i) v->resumeSound(audio,sound);
    SDL_Delay(80);assert(v->getTrackPosition(audio,sound)>=running);
    for (int i = 0; i < 20; ++i) {
        v->setTrackPosition(audio, sound, i * 0.03f);
        v->pauseSound(audio, sound); v->resumeSound(audio, sound);
    }
    // Resource IDs must control the stream, and stale playback handles must
    // not affect the next sound occupying the same backend slot.
    v->stopSound(audio, stream);
    assert(!v->isPlaying(audio, sound));
    int replacement = v->playSound(audio, stream, 0, true);
    assert(replacement != sound);
    v->stopSound(audio, sound);
    assert(v->isPlaying(audio, replacement));
    assert(v->isPlaying(audio, stream));
    assert(v->destroyStream(audio, stream));
    assert(!v->isPlaying(audio, sound));
    v->destroy(audio);
    assert(frees == 2);
    puts("PASS: worker refills without frames; pause, seek, teardown and sparse groups");
    return 0;
}
