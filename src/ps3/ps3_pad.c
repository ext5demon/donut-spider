#include "ps3_pad.h"
#include "ps3_stick.h"
#include <string.h>

void PS3Pad_init(PS3PadState* pad) {
    memset(pad,0,sizeof(*pad));pad->port=-1;
}
static void disconnect(PS3PadState* pad) {
    pad->port=-1;pad->valid=false;pad->suppressConfirm=false;
    memset(&pad->data,0,sizeof(pad->data));memset(pad->stickHeld,0,sizeof(pad->stickHeld));
}
void PS3Pad_poll(PS3PadState* pad) {
    padInfo info={0};int32_t result=ioPadGetInfo(&info);
    if (pad->recoveryDelay) --pad->recoveryDelay;
    if (result==(int32_t)PAD_ERROR_NOT_INITIALIZED) {
        disconnect(pad);
        if (!pad->recoveryDelay) {
            pad->recoveryDelay=60;
            int32_t init=ioPadInit(7);
            if (init==0 || init==(int32_t)PAD_ERROR_ALREADY_INITIALIZED) result=ioPadGetInfo(&info);
        }
    }
    if (result!=0) {pad->error=result;return;}
    pad->error=0;pad->recoveryDelay=0;
    // Retain the controller used in the picker while it remains connected.
    // A spare controller connecting to a lower slot must not steal control.
    int port=pad->port;
    if (port<0 || !(info.status[port]&1)) {
        port=-1;
        for (int i=0;i<7;++i) if (info.status[i]&1) {port=i;break;}
    }
    if (port!=pad->port) {disconnect(pad);pad->port=port;}
    if (port<0) return;
    padData data={0};result=ioPadGetData((uint32_t)port,&data);
    if (result!=0) {pad->error=result;return;}
    // Empty samples and transient failures must preserve the last valid data.
    if (data.len>PAD_BUTTON_OFFSET_DIGITAL2) {
        if (data.len<=PAD_BUTTON_OFFSET_ANALOG_LEFT_X) data.button[PAD_BUTTON_OFFSET_ANALOG_LEFT_X]=128;
        if (data.len<=PAD_BUTTON_OFFSET_ANALOG_LEFT_Y) data.button[PAD_BUTTON_OFFSET_ANALOG_LEFT_Y]=128;
        pad->data=data;pad->valid=true;
    }
}
void PS3Pad_beginGameplay(PS3PadState* pad) {
    memset(pad->previousKeys,0,sizeof(pad->previousKeys));memset(pad->stickHeld,0,sizeof(pad->stickHeld));
    // Consume only the launch press, never wait for every stick/button to be
    // neutral. Movement and the other buttons work immediately after loading.
    pad->suppressConfirm=pad->valid && (pad->data.button[PAD_BUTTON_OFFSET_DIGITAL2]&PAD_CTRL_CROSS)!=0;
}
void PS3Pad_updateKeyboard(PS3PadState* pad,RunnerKeyboardState* keyboard) {
    bool keys[GML_KEY_COUNT]={0};
    if (pad->valid) {
        unsigned d1=pad->data.button[PAD_BUTTON_OFFSET_DIGITAL1],d2=pad->data.button[PAD_BUTTON_OFFSET_DIGITAL2];
        if (!(d2&PAD_CTRL_CROSS)) pad->suppressConfirm=false;
        keys[VK_UP]=(d1&PAD_CTRL_UP)!=0;keys[VK_DOWN]=(d1&PAD_CTRL_DOWN)!=0;
        keys[VK_LEFT]=(d1&PAD_CTRL_LEFT)!=0;keys[VK_RIGHT]=(d1&PAD_CTRL_RIGHT)!=0;
        keys['Z']=(d2&PAD_CTRL_CROSS)!=0 && !pad->suppressConfirm;
        keys['X']=(d2&(PAD_CTRL_CIRCLE|PAD_CTRL_SQUARE))!=0;
        keys['C']=(d2&PAD_CTRL_TRIANGLE)!=0;
        keys[VK_PAGEDOWN]=(d2&PAD_CTRL_L1)!=0;keys[VK_PAGEUP]=(d2&PAD_CTRL_R1)!=0;
        keys[VK_F10]=(d2&PAD_CTRL_L2)!=0;keys[VK_F12]=(d1&PAD_CTRL_START)!=0;
#ifdef DONUT_SPIDER_DEV_BUILD
        keys[VK_F12]|=(d1&PAD_CTRL_SELECT)!=0;
#endif
        const unsigned axes[]={PAD_BUTTON_OFFSET_ANALOG_LEFT_X,PAD_BUTTON_OFFSET_ANALOG_LEFT_X,
            PAD_BUTTON_OFFSET_ANALOG_LEFT_Y,PAD_BUTTON_OFFSET_ANALOG_LEFT_Y};
        const int signs[]={-1,1,-1,1};const int codes[]={VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN};
        for (unsigned i=0;i<4;++i) {
            pad->stickHeld[i]=PS3Stick_direction((uint8_t)pad->data.button[axes[i]],signs[i],pad->stickHeld[i]);
            keys[codes[i]]|=pad->stickHeld[i];
        }
    }
    for (int key=0;key<GML_KEY_COUNT;++key) {
        if (keys[key] && !pad->previousKeys[key]) RunnerKeyboard_onKeyDown(keyboard,key);
        if (!keys[key] && pad->previousKeys[key]) RunnerKeyboard_onKeyUp(keyboard,key);
        pad->previousKeys[key]=keys[key];
    }
}
