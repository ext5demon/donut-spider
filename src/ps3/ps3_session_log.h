#ifndef DONUT_SPIDER_PS3_SESSION_LOG_H
#define DONUT_SPIDER_PS3_SESSION_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool PS3SessionLog_open(const char* appDirectory);
void PS3SessionLog_event(const char* format, ...);
void PS3SessionLog_heartbeat(const char* roomName, int32_t roomIndex, int32_t frame,
                             int32_t instances, int32_t structs, size_t heapBytes,
                             size_t textureBytes, size_t surfaceBytes);
void PS3SessionLog_close(const char* reason);
const char* PS3SessionLog_path(void);

#endif
