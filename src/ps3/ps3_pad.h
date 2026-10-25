#ifndef SPIDER_PS3_PAD_H
#define SPIDER_PS3_PAD_H
#include <io/pad.h>
#include "runner_keyboard.h"

// One snapshot survives the picker -> game handoff. ioPadGetData reports only
// changed samples, so replacing this with a new poller loses held input.
typedef struct {
    int port;
    padData data;
    bool valid, suppressConfirm;
    bool previousKeys[GML_KEY_COUNT], stickHeld[4];
    int32_t error;
    unsigned recoveryDelay;
} PS3PadState;

void PS3Pad_init(PS3PadState* pad);
void PS3Pad_poll(PS3PadState* pad);
void PS3Pad_beginGameplay(PS3PadState* pad);
void PS3Pad_updateKeyboard(PS3PadState* pad,RunnerKeyboardState* keyboard);
#endif
