#ifndef _SPIDER_DONUT_PS3_WAD_PICKER_H_
#define _SPIDER_DONUT_PS3_WAD_PICKER_H_

#include "common.h"

// Blocks on a controller-driven storage browser and returns an owned absolute
// path to the selected .win. Returns nullptr when the XMB requests exit.
char* PS3WadPicker_select(const char* bundledWadPath, const bool* shouldExit);

#endif
