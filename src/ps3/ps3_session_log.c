#include "ps3_session_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SESSION_LOG_PATH_MAX 1024

static FILE* sessionFile = NULL;
static char sessionPath[SESSION_LOG_PATH_MAX] = {0};
static time_t sessionStartedAt = 0;

static bool appendPath(char* output, size_t outputSize, const char* directory, const char* name) {
    if (output == NULL || outputSize == 0 || directory == NULL || name == NULL) return false;
    const size_t directoryLength = strlen(directory);
    const bool needsSlash = directoryLength > 0 && directory[directoryLength - 1] != '/' && directory[directoryLength - 1] != '\\';
    const int written = snprintf(output, outputSize, "%s%s%s", directory, needsSlash ? "/" : "", name);
    return written >= 0 && (size_t)written < outputSize;
}

static void writePrefix(void) {
    if (sessionFile == NULL) return;
    const time_t now = time(NULL);
    const long elapsed = sessionStartedAt > 0 && now >= sessionStartedAt ? (long)(now - sessionStartedAt) : 0;
    fprintf(sessionFile, "[%06lds] ", elapsed);
}

bool PS3SessionLog_open(const char* appDirectory) {
    if (sessionFile != NULL) return true;

    char logDirectory[SESSION_LOG_PATH_MAX];
    if (!appendPath(logDirectory, sizeof(logDirectory), appDirectory, "logs")) return false;
    if (mkdir(logDirectory, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "Session log: could not create %s (errno=%d)\n", logDirectory, errno);
        return false;
    }

    sessionStartedAt = time(NULL);
    struct tm brokenDown;
    memset(&brokenDown, 0, sizeof(brokenDown));
    struct tm* local = localtime(&sessionStartedAt);
    if (local != NULL) brokenDown = *local;

    char fileName[96];
    snprintf(fileName, sizeof(fileName), "session-%04d%02d%02d-%02d%02d%02d.log",
             brokenDown.tm_year + 1900, brokenDown.tm_mon + 1, brokenDown.tm_mday,
             brokenDown.tm_hour, brokenDown.tm_min, brokenDown.tm_sec);
    if (!appendPath(sessionPath, sizeof(sessionPath), logDirectory, fileName)) return false;

    struct stat existing;
    for (int suffix = 1; stat(sessionPath, &existing) == 0 && suffix <= 99; suffix++) {
        snprintf(fileName, sizeof(fileName), "session-%04d%02d%02d-%02d%02d%02d-%02d.log",
                 brokenDown.tm_year + 1900, brokenDown.tm_mon + 1, brokenDown.tm_mday,
                 brokenDown.tm_hour, brokenDown.tm_min, brokenDown.tm_sec, suffix);
        if (!appendPath(sessionPath, sizeof(sessionPath), logDirectory, fileName)) return false;
    }
    if (stat(sessionPath, &existing) == 0) return false;

    sessionFile = fopen(sessionPath, "a");
    if (sessionFile == NULL) {
        fprintf(stderr, "Session log: could not open %s (errno=%d)\n", sessionPath, errno);
        sessionPath[0] = '\0';
        return false;
    }

    setvbuf(sessionFile, NULL, _IOLBF, 0);
    fprintf(sessionFile, "Donut Spider %s (%s)\n", DONUT_SPIDER_VERSION, DONUT_SPIDER_DISPLAY_NAME);
    fprintf(sessionFile, "Status: Pre-alpha\n");
    fprintf(sessionFile, "Log path: %s\n", sessionPath);
    fflush(sessionFile);
    printf("Session log: %s\n", sessionPath);
    return true;
}

void PS3SessionLog_event(const char* format, ...) {
    if (sessionFile == NULL || format == NULL) return;
    writePrefix();
    va_list args;
    va_start(args, format);
    vfprintf(sessionFile, format, args);
    va_end(args);
    fputc('\n', sessionFile);
    fflush(sessionFile);
}

void PS3SessionLog_heartbeat(const char* roomName, int32_t roomIndex, int32_t frame,
                             int32_t instances, int32_t structs, size_t heapBytes,
                             size_t textureBytes, size_t surfaceBytes) {
    PS3SessionLog_event(
        "HEARTBEAT room=%s room_index=%d frame=%d instances=%d structs=%d heap=%u texture_ram=%u surface_ram=%u",
        roomName != NULL ? roomName : "?", roomIndex, frame, instances, structs,
        (unsigned)heapBytes, (unsigned)textureBytes, (unsigned)surfaceBytes);
}

void PS3SessionLog_close(const char* reason) {
    if (sessionFile == NULL) return;
    PS3SessionLog_event("SESSION END reason=%s", reason != NULL ? reason : "unknown");
    fclose(sessionFile);
    sessionFile = NULL;
}

const char* PS3SessionLog_path(void) {
    return sessionPath[0] != '\0' ? sessionPath : NULL;
}
