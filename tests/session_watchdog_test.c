#include "ps3_session_log.h"
#include <SDL2/SDL_timer.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char directory[] = "/tmp/spider-watchdog-XXXXXX";
    assert(mkdtemp(directory));
    assert(PS3SessionLog_open(directory));
    PS3SessionLog_phase("step", 42, 7);
    SDL_Delay(10500);
    PS3SessionLog_close("watchdog test");
    char output[8192] = {0};
    const char* path = PS3SessionLog_path();
    FILE* log = fopen(path, "r"); assert(log);
    assert(fread(output, 1, sizeof(output) - 1, log) > 0);
    fclose(log);
    const char* stall = strstr(output, "RUNNER STALL: phase=step room_index=42 frame=7");
    assert(stall && !strstr(stall + 1, "RUNNER STALL:"));
    assert(strstr(output, "SESSION END reason=watchdog test"));
    unlink(path);
    char logs[256]; snprintf(logs, sizeof(logs), "%s/logs", directory);
    rmdir(logs); rmdir(directory);
    puts("PASS: stalled phase is recorded once and watchdog joins on shutdown");
    return 0;
}
