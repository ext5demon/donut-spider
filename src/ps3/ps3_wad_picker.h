#ifndef _SPIDER_DONUT_PS3_WAD_PICKER_H_
#define _SPIDER_DONUT_PS3_WAD_PICKER_H_

#include "common.h"
#include "ps3_pad.h"

// Discovers chapters across mounted storage, then presents the chapter select
// and optional file browser. Returns an owned path, or nullptr on exit.
char* PS3WadPicker_select(const char* bundledWadPath, const bool* shouldExit, PS3PadState* controller);

#endif
