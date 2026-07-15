#ifndef _BS_VIDEO_PLAYER_H_
#define _BS_VIDEO_PLAYER_H_

#include "common.h"
#include <stdint.h>

#ifndef RUNNER_DEFINED
#define RUNNER_DEFINED
typedef struct Runner Runner;
#endif
#ifndef VIDEO_PLAYER_DEFINED
#define VIDEO_PLAYER_DEFINED
typedef struct VideoPlayer VideoPlayer;
#endif

enum {
    VIDEO_STATUS_CLOSED = 0,
    VIDEO_STATUS_PREPARING = 1,
    VIDEO_STATUS_PLAYING = 2,
    VIDEO_STATUS_PAUSED = 3,
};

enum {
    VIDEO_FORMAT_RGBA = 0,
    VIDEO_FORMAT_YUV = 1,
};

VideoPlayer* VideoPlayer_create(void);
void VideoPlayer_free(VideoPlayer* player, Runner* runner);

// Opens <path>.bsv, the streamed sidecar produced by tools/transcode_video.py.
// The original MP4 is never loaded into RAM by the runner.
bool VideoPlayer_open(VideoPlayer* player, Runner* runner, const char* path);
void VideoPlayer_close(VideoPlayer* player, Runner* runner);
void VideoPlayer_update(VideoPlayer* player, Runner* runner);

// Returns GameMaker's video_draw status (0, -1, -2) and the reusable RGBA
// surface ID through outSurface when a frame is available.
int32_t VideoPlayer_draw(VideoPlayer* player, Runner* runner, int32_t* outSurface);

int32_t VideoPlayer_getStatus(const VideoPlayer* player);
int32_t VideoPlayer_getFormat(const VideoPlayer* player);
double VideoPlayer_getDurationMs(const VideoPlayer* player);
double VideoPlayer_getPositionMs(const VideoPlayer* player);
void VideoPlayer_setVolume(VideoPlayer* player, Runner* runner, float volume);
void VideoPlayer_setLooping(VideoPlayer* player, bool looping);
void VideoPlayer_pause(VideoPlayer* player, Runner* runner);
void VideoPlayer_resume(VideoPlayer* player, Runner* runner);

// Returns "video_start" or "video_end" once and clears that pending event.
const char* VideoPlayer_pollAsyncEvent(VideoPlayer* player);

#endif /* _BS_VIDEO_PLAYER_H_ */
