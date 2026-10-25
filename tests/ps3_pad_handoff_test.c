#include "ps3_pad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static padInfo info;
static padData next;
static int32_t infoError,dataError,initError;
static unsigned initCalls,lastPolledPort;
int32_t ioPadInit(uint32_t count) {
    assert(count==7);++initCalls;
    if (!initError) infoError=0;
    return initError;
}
int32_t ioPadGetInfo(padInfo* out) {if (infoError) return infoError;*out=info;return 0;}
int32_t ioPadGetData(uint32_t port,padData* out) {
    assert(port<7 && (info.status[port]&1));lastPolledPort=port;
    if (dataError) return dataError;
    *out=next;return 0;
}
static void sample(unsigned d1,unsigned d2,unsigned x,unsigned y) {
    memset(&next,0,sizeof(next));next.len=8;
    next.button[2]=d1;next.button[3]=d2;next.button[6]=x;next.button[7]=y;
}
static void frame(PS3PadState* pad,RunnerKeyboardState* kb) {
    RunnerKeyboard_beginFrame(kb);PS3Pad_poll(pad);PS3Pad_updateKeyboard(pad,kb);
}
int main(void) {
    PS3PadState pad;PS3Pad_init(&pad);RunnerKeyboardState* kb=RunnerKeyboard_create();
    // Slot 3 selects Play. Slot 1 has no controller; the dev6 game loop ignored slot 3.
    info.status[2]=1;sample(PAD_CTRL_DOWN,PAD_CTRL_CROSS,128,128);
    PS3Pad_poll(&pad);assert(pad.port==2 && pad.valid && lastPolledPort==2);
    PS3Pad_beginGameplay(&pad);next.len=0;frame(&pad,kb);
    assert(RunnerKeyboard_checkPressed(kb,VK_DOWN));assert(!RunnerKeyboard_check(kb,'Z'));
    assert(pad.port==2 && lastPolledPort==2); // same slot and cached packet after picker teardown
    sample(0,0,128,128);frame(&pad,kb);assert(RunnerKeyboard_checkReleased(kb,VK_DOWN));
    sample(0,PAD_CTRL_CROSS,128,128);frame(&pad,kb);assert(RunnerKeyboard_checkPressed(kb,'Z'));
    next.len=0;frame(&pad,kb);assert(RunnerKeyboard_check(kb,'Z') && !RunnerKeyboard_checkPressed(kb,'Z'));
    infoError=-7;frame(&pad,kb);assert(RunnerKeyboard_check(kb,'Z') && pad.error==-7);infoError=0;
    dataError=-8;frame(&pad,kb);assert(RunnerKeyboard_check(kb,'Z') && pad.error==-8);dataError=0;
    info.status[2]=0;frame(&pad,kb);assert(pad.port==-1 && !pad.valid && RunnerKeyboard_checkReleased(kb,'Z'));

    // Reconnection to another slot is live, and a lower-numbered spare cannot steal control.
    info.status[5]=1;sample(PAD_CTRL_RIGHT,0,128,128);frame(&pad,kb);
    assert(pad.port==5 && lastPolledPort==5 && RunnerKeyboard_checkPressed(kb,VK_RIGHT));
    info.status[0]=1;next.len=0;frame(&pad,kb);assert(pad.port==5 && lastPolledPort==5);
    info.status[5]=0;sample(PAD_CTRL_LEFT,0,128,128);frame(&pad,kb);
    assert(pad.port==0 && lastPolledPort==0 && RunnerKeyboard_checkReleased(kb,VK_RIGHT));
    assert(RunnerKeyboard_checkPressed(kb,VK_LEFT));
    sample(0,0,193,63);frame(&pad,kb);assert(RunnerKeyboard_check(kb,VK_RIGHT) && RunnerKeyboard_check(kb,VK_UP));
    sample(0,0,180,75);frame(&pad,kb);assert(RunnerKeyboard_check(kb,VK_RIGHT) && RunnerKeyboard_check(kb,VK_UP));
    sample(0,0,128,128);frame(&pad,kb);assert(!RunnerKeyboard_check(kb,VK_ANYKEY));

    // Partial digital packets must not create phantom up/left from missing analog values.
    sample(0,PAD_CTRL_TRIANGLE,0,0);next.len=4;frame(&pad,kb);
    assert(RunnerKeyboard_checkPressed(kb,'C') && !RunnerKeyboard_check(kb,VK_UP) && !RunnerKeyboard_check(kb,VK_LEFT));
    sample(0,PAD_CTRL_CIRCLE|PAD_CTRL_SQUARE,128,128);frame(&pad,kb);assert(RunnerKeyboard_checkPressed(kb,'X'));
    sample(0,PAD_CTRL_SQUARE,128,128);frame(&pad,kb);assert(RunnerKeyboard_check(kb,'X') && !RunnerKeyboard_checkReleased(kb,'X'));
    sample(0,0,128,128);frame(&pad,kb);assert(RunnerKeyboard_checkReleased(kb,'X'));

    // A pad service restart cannot permanently disable gameplay input.
    infoError=(int32_t)PAD_ERROR_NOT_INITIALIZED;sample(PAD_CTRL_DOWN,0,128,128);frame(&pad,kb);
    assert(initCalls==1 && pad.port==0 && RunnerKeyboard_checkPressed(kb,VK_DOWN));
    infoError=(int32_t)PAD_ERROR_NOT_INITIALIZED;initError=-9;frame(&pad,kb);
    unsigned attempts=initCalls;
    for (unsigned i=0;i<10;++i) frame(&pad,kb);
    assert(initCalls==attempts && pad.port==-1 && !RunnerKeyboard_check(kb,VK_ANYKEY));
    infoError=0;initError=0;sample(0,PAD_CTRL_CROSS,128,128);frame(&pad,kb);
    assert(RunnerKeyboard_checkPressed(kb,'Z'));
    RunnerKeyboard_free(kb);
    puts("PASS: picker-to-game input, non-first slots, held/empty/error samples, reconnects, analogs and pad recovery");
}
