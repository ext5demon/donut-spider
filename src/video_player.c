#include "video_player.h"

#include "runner.h"
#include "utils.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_VIDEO
#include "image_decoder.h"
#endif

typedef struct {
    uint32_t offset;
    uint32_t size;
} VideoFrameEntry;

struct VideoPlayer {
    void* fileHandle;
    VideoFrameEntry* frames;
    uint8_t* compressedFrame;
    uint32_t compressedCapacity;
    uint32_t frameCount;
    uint32_t width;
    uint32_t height;
    uint32_t fpsNumerator;
    uint32_t fpsDenominator;
    double durationMs;
    double positionMs;
    int32_t surfaceId;
    int32_t decodedFrame;
    int32_t status;
    int32_t streamIndex;
    int32_t soundInstance;
    float volume;
    bool looping;
    bool ended;
    bool startEventPending;
    bool endEventPending;
};

static uint32_t readLe32(const uint8_t* bytes) {
    return (uint32_t) bytes[0]
        | ((uint32_t) bytes[1] << 8)
        | ((uint32_t) bytes[2] << 16)
        | ((uint32_t) bytes[3] << 24);
}

static bool readExact(FileSystem* fs, void* handle, void* dst, int32_t size) {
    return fs->vtable->binaryRead(fs, handle, dst, size) == size;
}

static char* appendSuffix(const char* path, const char* suffix) {
    size_t pathLen = strlen(path);
    size_t suffixLen = strlen(suffix);
    char* result = (char*) safeMalloc(pathLen + suffixLen + 1);
    memcpy(result, path, pathLen);
    memcpy(result + pathLen, suffix, suffixLen + 1);
    return result;
}

VideoPlayer* VideoPlayer_create(void) {
    VideoPlayer* player = (VideoPlayer*) safeCalloc(1, sizeof(VideoPlayer));
    player->surfaceId = -1;
    player->decodedFrame = -1;
    player->streamIndex = -1;
    player->soundInstance = -1;
    player->volume = 1.0f;
    player->status = VIDEO_STATUS_CLOSED;
    return player;
}

void VideoPlayer_close(VideoPlayer* player, Runner* runner) {
    if (player == nullptr) return;

    if (runner != nullptr && runner->audioSystem != nullptr) {
        if (player->soundInstance >= 0) {
            runner->audioSystem->vtable->stopSound(runner->audioSystem, player->soundInstance);
        }
        if (player->streamIndex >= 0) {
            runner->audioSystem->vtable->destroyStream(runner->audioSystem, player->streamIndex);
        }
    }
    player->soundInstance = -1;
    player->streamIndex = -1;

    if (runner != nullptr && runner->renderer != nullptr && player->surfaceId >= 0) {
        runner->renderer->vtable->surfaceFree(runner->renderer, player->surfaceId);
    }
    player->surfaceId = -1;

    if (runner != nullptr && runner->fileSystem != nullptr && player->fileHandle != nullptr) {
        runner->fileSystem->vtable->binaryClose(runner->fileSystem, player->fileHandle);
    }
    player->fileHandle = nullptr;
    free(player->frames);
    player->frames = nullptr;
    free(player->compressedFrame);
    player->compressedFrame = nullptr;
    player->compressedCapacity = 0;
    player->frameCount = 0;
    player->width = 0;
    player->height = 0;
    player->fpsNumerator = 0;
    player->fpsDenominator = 0;
    player->durationMs = 0.0;
    player->positionMs = 0.0;
    player->decodedFrame = -1;
    player->status = VIDEO_STATUS_CLOSED;
    player->ended = false;
    player->startEventPending = false;
    player->endEventPending = false;
}

void VideoPlayer_free(VideoPlayer* player, Runner* runner) {
    if (player == nullptr) return;
    VideoPlayer_close(player, runner);
    free(player);
}

bool VideoPlayer_open(VideoPlayer* player, Runner* runner, const char* path) {
    if (player == nullptr || runner == nullptr || path == nullptr || path[0] == '\0') return false;
    VideoPlayer_close(player, runner);

#ifndef ENABLE_VIDEO
    fprintf(stderr, "Video: support is disabled in this build (requested '%s')\n", path);
    return false;
#else
    FileSystem* fs = runner->fileSystem;
    char* sidecarPath = appendSuffix(path, ".bsv");
    void* handle = fs->vtable->binaryOpen(fs, sidecarPath, GML_FILE_BIN_READ);
    if (handle == nullptr) {
        fprintf(stderr, "Video: streamed sidecar not found: %s\n", sidecarPath);
        free(sidecarPath);
        return false;
    }

    uint8_t header[32];
    if (!readExact(fs, handle, header, (int32_t) sizeof(header)) || memcmp(header, "BSV1", 4) != 0) {
        fprintf(stderr, "Video: invalid sidecar header: %s\n", sidecarPath);
        fs->vtable->binaryClose(fs, handle);
        free(sidecarPath);
        return false;
    }

    uint32_t width = readLe32(header + 4);
    uint32_t height = readLe32(header + 8);
    uint32_t fpsNumerator = readLe32(header + 12);
    uint32_t fpsDenominator = readLe32(header + 16);
    uint32_t frameCount = readLe32(header + 20);
    uint32_t durationMs = readLe32(header + 24);
    int32_t fileSize = fs->vtable->binarySize(fs, handle);
    uint64_t indexEnd = 32ULL + (uint64_t) frameCount * 8ULL;
    bool headerValid = width > 0 && width <= 4096
        && height > 0 && height <= 4096
        && fpsNumerator > 0 && fpsDenominator > 0
        && frameCount > 0 && frameCount <= 1000000
        && durationMs > 0 && indexEnd <= (uint64_t) fileSize;
    if (!headerValid) {
        fprintf(stderr, "Video: invalid sidecar metadata: %s\n", sidecarPath);
        fs->vtable->binaryClose(fs, handle);
        free(sidecarPath);
        return false;
    }

    VideoFrameEntry* frames = (VideoFrameEntry*) safeMalloc((size_t) frameCount * sizeof(VideoFrameEntry));
    uint8_t entryBytes[8];
    bool indexValid = true;
    for (uint32_t i = 0; i < frameCount; i++) {
        if (!readExact(fs, handle, entryBytes, 8)) {
            indexValid = false;
            break;
        }
        frames[i].offset = readLe32(entryBytes);
        frames[i].size = readLe32(entryBytes + 4);
        uint64_t frameEnd = (uint64_t) frames[i].offset + (uint64_t) frames[i].size;
        if (frames[i].size == 0 || frames[i].offset < indexEnd || frameEnd > (uint64_t) fileSize) {
            indexValid = false;
            break;
        }
    }
    if (!indexValid) {
        fprintf(stderr, "Video: corrupt frame index: %s\n", sidecarPath);
        free(frames);
        fs->vtable->binaryClose(fs, handle);
        free(sidecarPath);
        return false;
    }

    player->fileHandle = handle;
    player->frames = frames;
    player->frameCount = frameCount;
    player->width = width;
    player->height = height;
    player->fpsNumerator = fpsNumerator;
    player->fpsDenominator = fpsDenominator;
    player->durationMs = (double) durationMs;
    player->positionMs = 0.0;
    player->decodedFrame = -1;
    player->status = VIDEO_STATUS_PLAYING;
    player->ended = false;
    player->startEventPending = true;

    char* audioPath = appendSuffix(sidecarPath, ".ogg");
    if (fs->vtable->fileExists(fs, audioPath)) {
        player->streamIndex = runner->audioSystem->vtable->createStream(runner->audioSystem, audioPath);
        if (player->streamIndex >= 0) {
            player->soundInstance = runner->audioSystem->vtable->playSound(runner->audioSystem, player->streamIndex, 10, false);
            if (player->soundInstance >= 0) {
                runner->audioSystem->vtable->setSoundGain(runner->audioSystem, player->soundInstance, player->volume, 0);
            }
        }
    }
    free(audioPath);

    fprintf(stderr, "Video: opened %s (%ux%u, %u frames, %.3fs, streamed RGBA)\n",
        sidecarPath, width, height, frameCount, player->durationMs / 1000.0);
    free(sidecarPath);
    return true;
#endif
}

void VideoPlayer_update(VideoPlayer* player, Runner* runner) {
    if (player == nullptr || runner == nullptr || player->fileHandle == nullptr) return;
    if (player->status != VIDEO_STATUS_PLAYING || player->ended) return;

    double elapsedMs = runner->deltaTime / 1000.0;
    if (!isfinite(elapsedMs) || elapsedMs < 0.0) elapsedMs = 0.0;
    // Automated/headless playback deliberately runs without a wall-clock frame
    // limiter. Advance by at least one game step so recorded input routes stay
    // deterministic and a 41-second clip does not require 41 real seconds in CI.
    if (runner->currentRoom != nullptr && runner->currentRoom->speed > 0) {
        double gameStepMs = 1000.0 / (double) runner->currentRoom->speed;
        if (elapsedMs < gameStepMs) elapsedMs = gameStepMs;
    }
    player->positionMs += elapsedMs;
    if (player->positionMs < player->durationMs) return;

    if (player->looping) {
        player->positionMs = fmod(player->positionMs, player->durationMs);
        player->decodedFrame = -1;
        if (player->soundInstance >= 0) {
            runner->audioSystem->vtable->setTrackPosition(runner->audioSystem, player->soundInstance, (float) (player->positionMs / 1000.0));
        }
        return;
    }

    player->positionMs = player->durationMs;
    player->ended = true;
    player->status = VIDEO_STATUS_CLOSED;
    player->endEventPending = true;
    if (player->soundInstance >= 0) {
        runner->audioSystem->vtable->stopSound(runner->audioSystem, player->soundInstance);
    }
}

int32_t VideoPlayer_draw(VideoPlayer* player, Runner* runner, int32_t* outSurface) {
    if (outSurface != nullptr) *outSurface = -1;
    if (player == nullptr || runner == nullptr || player->fileHandle == nullptr) return -1;
    if (player->ended) return -2;

#ifndef ENABLE_VIDEO
    return -1;
#else
    double framePosition = player->positionMs * (double) player->fpsNumerator
        / (1000.0 * (double) player->fpsDenominator);
    uint32_t frameIndex = (uint32_t) framePosition;
    if (frameIndex >= player->frameCount) frameIndex = player->frameCount - 1;

    if ((int32_t) frameIndex != player->decodedFrame) {
        VideoFrameEntry entry = player->frames[frameIndex];
        if (entry.size > player->compressedCapacity) {
            player->compressedFrame = (uint8_t*) safeRealloc(player->compressedFrame, entry.size);
            player->compressedCapacity = entry.size;
        }
        FileSystem* fs = runner->fileSystem;
        if (!fs->vtable->binarySeek(fs, player->fileHandle, (int32_t) entry.offset)
            || !readExact(fs, player->fileHandle, player->compressedFrame, (int32_t) entry.size)) {
            fprintf(stderr, "Video: failed to read frame %u\n", frameIndex);
            return -1;
        }

        int decodedWidth = 0;
        int decodedHeight = 0;
        uint8_t* rgba = ImageDecoder_decodeToRgba(player->compressedFrame, entry.size, false, &decodedWidth, &decodedHeight);
        if (rgba == nullptr || decodedWidth != (int) player->width || decodedHeight != (int) player->height) {
            fprintf(stderr, "Video: failed to decode frame %u\n", frameIndex);
            free(rgba);
            return -1;
        }

        Renderer* renderer = runner->renderer;
        if (renderer->vtable->surfaceUpdatePixels == nullptr) {
            fprintf(stderr, "Video: renderer does not support streamed surface uploads\n");
            free(rgba);
            return -1;
        }
        if (player->surfaceId < 0) {
            player->surfaceId = renderer->vtable->createSurface(renderer, (int32_t) player->width, (int32_t) player->height);
        }
        bool uploaded = player->surfaceId >= 0
            && renderer->vtable->surfaceUpdatePixels(renderer, player->surfaceId, rgba, (int32_t) player->width, (int32_t) player->height);
        free(rgba);
        if (!uploaded) {
            fprintf(stderr, "Video: failed to upload frame %u\n", frameIndex);
            return -1;
        }
        player->decodedFrame = (int32_t) frameIndex;
    }

    if (outSurface != nullptr) *outSurface = player->surfaceId;
    return 0;
#endif
}

int32_t VideoPlayer_getStatus(const VideoPlayer* player) {
    return player != nullptr ? player->status : VIDEO_STATUS_CLOSED;
}

int32_t VideoPlayer_getFormat(MAYBE_UNUSED const VideoPlayer* player) {
    return VIDEO_FORMAT_RGBA;
}

double VideoPlayer_getDurationMs(const VideoPlayer* player) {
    return player != nullptr ? player->durationMs : 0.0;
}

double VideoPlayer_getPositionMs(const VideoPlayer* player) {
    return player != nullptr ? player->positionMs : 0.0;
}

void VideoPlayer_setVolume(VideoPlayer* player, Runner* runner, float volume) {
    if (player == nullptr) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    player->volume = volume;
    if (runner != nullptr && player->soundInstance >= 0) {
        runner->audioSystem->vtable->setSoundGain(runner->audioSystem, player->soundInstance, volume, 0);
    }
}

void VideoPlayer_setLooping(VideoPlayer* player, bool looping) {
    if (player != nullptr) player->looping = looping;
}

void VideoPlayer_pause(VideoPlayer* player, Runner* runner) {
    if (player == nullptr || player->status != VIDEO_STATUS_PLAYING) return;
    player->status = VIDEO_STATUS_PAUSED;
    if (runner != nullptr && player->soundInstance >= 0) {
        runner->audioSystem->vtable->pauseSound(runner->audioSystem, player->soundInstance);
    }
}

void VideoPlayer_resume(VideoPlayer* player, Runner* runner) {
    if (player == nullptr || player->status != VIDEO_STATUS_PAUSED) return;
    player->status = VIDEO_STATUS_PLAYING;
    if (runner != nullptr && player->soundInstance >= 0) {
        runner->audioSystem->vtable->resumeSound(runner->audioSystem, player->soundInstance);
    }
}

const char* VideoPlayer_pollAsyncEvent(VideoPlayer* player) {
    if (player == nullptr) return nullptr;
    if (player->startEventPending) {
        player->startEventPending = false;
        return "video_start";
    }
    if (player->endEventPending) {
        player->endEventPending = false;
        return "video_end";
    }
    return nullptr;
}
