#ifndef PICKER_UI_H
#define PICKER_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICKER_ROWS 7
typedef struct {
    bool chapters, confirming;
    int selected, first, count, choice, footer;
    const char* names[PICKER_ROWS];
    bool enabled[PICKER_ROWS], directories[PICKER_ROWS];
    const char* location;
    const char* status;
    const char* message;
} PickerView;
typedef struct {
    unsigned texture, audioDevice;
    bool audioInitialized;
    float alpha, slide, age, fade, launchTime, launchDuration, voiceGain;
    const int16_t* voice;
    size_t voiceLength;
    double voicePosition;
} PickerUI;
bool PickerUI_init(PickerUI* ui);
void PickerUI_deinit(PickerUI* ui);
void PickerUI_enter(PickerUI* ui);
void PickerUI_update(PickerUI* ui,float seconds);
void PickerUI_launch(PickerUI* ui,int chapter);
bool PickerUI_launchFinished(const PickerUI* ui);
void PickerUI_sound(PickerUI* ui,bool select);
void PickerUI_draw(const PickerUI* ui,const PickerView* view,int width,int height);

// Menu key repeat uses elapsed time rather than display refresh rate.
typedef struct { unsigned held; int direction; float repeat; } PickerInput;
typedef struct { int vertical,horizontal; bool accept,back,reload; } PickerActions;
enum { PICKER_UP=1,PICKER_DOWN=2,PICKER_LEFT=4,PICKER_RIGHT=8,
       PICKER_ACCEPT=16,PICKER_BACK=32,PICKER_RELOAD=64 };
PickerActions PickerInput_update(PickerInput* input,unsigned held,float seconds);
#endif
