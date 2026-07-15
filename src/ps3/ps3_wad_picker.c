#include "ps3_wad_picker.h"

#include "ps3_overlay.h"
#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <io/pad.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>
#include <ps3gl.h>
#include "rsxutil.h"

#define PICKER_MAX_ENTRIES 512
#define PICKER_NAME_MAX 256
#define PICKER_PATH_MAX 1024
#define PICKER_VISIBLE_ROWS 20

typedef struct {
    char name[PICKER_NAME_MAX];
    char rootPath[96];
    bool directory;
    bool parent;
    bool directPath;
} PickerEntry;

typedef struct {
    bool up;
    bool down;
    bool accept;
    bool back;
    bool reload;
} PickerButtons;

static void copyBounded(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) return;
    if (source == nullptr) source = "?";
    size_t length = strlen(source);
    if (length >= capacity) length = capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool pathIsDirectory(const char* path) {
    struct stat st;
    return path != nullptr && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool pathIsFile(const char* path) {
    struct stat st;
    return path != nullptr && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool hasWinExtension(const char* name) {
    if (name == nullptr) return false;
    size_t len = strlen(name);
    return len >= 4 && name[len - 4] == '.' &&
        tolower((unsigned char) name[len - 3]) == 'w' &&
        tolower((unsigned char) name[len - 2]) == 'i' &&
        tolower((unsigned char) name[len - 1]) == 'n';
}

static void addEntry(PickerEntry* entries, int32_t* count, const char* name,
                     const char* rootPath, bool directory, bool parent, bool directPath) {
    if (*count >= PICKER_MAX_ENTRIES) return;
    PickerEntry* entry = &entries[(*count)++];
    memset(entry, 0, sizeof(*entry));
    copyBounded(entry->name, sizeof(entry->name), name);
    if (rootPath != nullptr) copyBounded(entry->rootPath, sizeof(entry->rootPath), rootPath);
    entry->directory = directory;
    entry->parent = parent;
    entry->directPath = directPath;
}

static int compareEntries(const void* lhsPtr, const void* rhsPtr) {
    const PickerEntry* lhs = (const PickerEntry*) lhsPtr;
    const PickerEntry* rhs = (const PickerEntry*) rhsPtr;
    if (lhs->parent != rhs->parent) return lhs->parent ? -1 : 1;
    if (lhs->directory != rhs->directory) return lhs->directory ? -1 : 1;
    const unsigned char* a = (const unsigned char*) lhs->name;
    const unsigned char* b = (const unsigned char*) rhs->name;
    while (*a != 0 && *b != 0) {
        int ca = tolower(*a++);
        int cb = tolower(*b++);
        if (ca != cb) return ca - cb;
    }
    return (int) *a - (int) *b;
}

static int32_t loadVirtualRoot(PickerEntry* entries, const char* bundledWadPath) {
    int32_t count = 0;
    if (pathIsFile(bundledWadPath)) {
        addEntry(entries, &count, "[BUNDLED] data.win", bundledWadPath, false, false, true);
    }
    const char* roots[][2] = {
        { "[HDD] Internal storage", "/dev_hdd0" },
        { "[DISC] Blu-ray / DVD", "/dev_bdvd" },
        { "[APP] App home", "/app_home" },
        { "[USB 0]", "/dev_usb000" }, { "[USB 1]", "/dev_usb001" },
        { "[USB 2]", "/dev_usb002" }, { "[USB 3]", "/dev_usb003" },
        { "[USB 4]", "/dev_usb004" }, { "[USB 5]", "/dev_usb005" },
        { "[USB 6]", "/dev_usb006" }, { "[USB 7]", "/dev_usb007" },
    };
    for (uint32_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (pathIsDirectory(roots[i][1])) addEntry(entries, &count, roots[i][0], roots[i][1], true, false, true);
    }
    return count;
}

static int32_t loadDirectory(PickerEntry* entries, const char* path) {
    int32_t count = 0;
    addEntry(entries, &count, "[..]", nullptr, true, true, false);
    DIR* dir = opendir(path);
    if (dir == nullptr) return count;
    struct dirent* item;
    while ((item = readdir(dir)) != nullptr && count < PICKER_MAX_ENTRIES) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
        char fullPath[PICKER_PATH_MAX];
        int written = snprintf(fullPath, sizeof(fullPath), "%s/%s", path, item->d_name);
        if (written < 0 || written >= (int) sizeof(fullPath)) continue;
        bool isDir = pathIsDirectory(fullPath);
        if (!isDir && !hasWinExtension(item->d_name)) continue;
        addEntry(entries, &count, item->d_name, nullptr, isDir, false, false);
    }
    closedir(dir);
    if (count > 1) qsort(entries + 1, (size_t) count - 1, sizeof(PickerEntry), compareEntries);
    return count;
}

static void parentDirectory(char* path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    char* slash = strrchr(path, '/');
    if (slash == nullptr || slash == path) {
        path[0] = '\0';
    } else {
        *slash = '\0';
    }
}

static bool makeEntryPath(const char* currentPath, const PickerEntry* entry,
                          char* outPath, size_t outSize) {
    int written;
    if (entry->directPath) written = snprintf(outPath, outSize, "%s", entry->rootPath);
    else written = snprintf(outPath, outSize, "%s/%s", currentPath, entry->name);
    return written >= 0 && (size_t) written < outSize;
}

static bool hasTextureBundle(const char* wadPath) {
    char path[PICKER_PATH_MAX];
    snprintf(path, sizeof(path), "%s", wadPath);
    char* slash = strrchr(path, '/');
    if (slash == nullptr) return false;
    size_t remaining = sizeof(path) - (size_t) (slash + 1 - path);
    snprintf(slash + 1, remaining, "TEXTURES.BIN");
    if (pathIsFile(path)) return true;
    snprintf(slash + 1, remaining, "textures.bin");
    return pathIsFile(path);
}

static PickerButtons pollButtons(void) {
    static bool previousUp, previousDown, previousAccept, previousBack, previousReload;
    PickerButtons edge = {0};
    padInfo info;
    ioPadGetInfo(&info);
    if (!info.status[0]) return edge;
    padData data;
    ioPadGetData(0, &data);
    if (data.len == 0) return edge;
    uint8_t d1 = (uint8_t) data.button[PAD_BUTTON_OFFSET_DIGITAL1];
    uint8_t d2 = (uint8_t) data.button[PAD_BUTTON_OFFSET_DIGITAL2];
    bool up = (d1 & PAD_CTRL_UP) != 0;
    bool down = (d1 & PAD_CTRL_DOWN) != 0;
    bool accept = (d2 & PAD_CTRL_CROSS) != 0;
    bool back = (d2 & PAD_CTRL_CIRCLE) != 0;
    bool reload = (d2 & PAD_CTRL_SQUARE) != 0;
    edge.up = up && !previousUp;
    edge.down = down && !previousDown;
    edge.accept = accept && !previousAccept;
    edge.back = back && !previousBack;
    edge.reload = reload && !previousReload;
    previousUp = up; previousDown = down; previousAccept = accept;
    previousBack = back; previousReload = reload;
    return edge;
}

static void appendText(char* output, size_t outputSize, size_t* used, const char* format, ...) {
    if (*used >= outputSize) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *used, outputSize - *used, format, args);
    va_end(args);
    if (written > 0) {
        size_t added = (size_t) written;
        *used += added < outputSize - *used ? added : outputSize - *used;
    }
}

char* PS3WadPicker_select(const char* bundledWadPath, const bool* shouldExit) {
    PickerEntry* entries = (PickerEntry*) safeCalloc(PICKER_MAX_ENTRIES, sizeof(PickerEntry));
    char currentPath[PICKER_PATH_MAX] = {0};
    char status[256] = "Choose a storage device, then a .win file.";
    int32_t count = loadVirtualRoot(entries, bundledWadPath);
    int32_t selected = 0;

    while (shouldExit == nullptr || !*shouldExit) {
        PickerButtons buttons = pollButtons();
        if (buttons.up && count > 0) selected = (selected + count - 1) % count;
        if (buttons.down && count > 0) selected = (selected + 1) % count;
        if (buttons.back) {
            if (currentPath[0] != '\0') {
                parentDirectory(currentPath);
                count = currentPath[0] == '\0' ? loadVirtualRoot(entries, bundledWadPath) : loadDirectory(entries, currentPath);
                selected = 0;
            }
        }
        if (buttons.reload) {
            count = currentPath[0] == '\0' ? loadVirtualRoot(entries, bundledWadPath) : loadDirectory(entries, currentPath);
            if (selected >= count) selected = count > 0 ? count - 1 : 0;
            snprintf(status, sizeof(status), "Storage list refreshed.");
        }
        if (buttons.accept && count > 0) {
            PickerEntry* entry = &entries[selected];
            if (entry->parent) {
                parentDirectory(currentPath);
                count = currentPath[0] == '\0' ? loadVirtualRoot(entries, bundledWadPath) : loadDirectory(entries, currentPath);
                selected = 0;
            } else {
                char selectedPath[PICKER_PATH_MAX];
                if (!makeEntryPath(currentPath, entry, selectedPath, sizeof(selectedPath))) {
                    snprintf(status, sizeof(status), "That path is too long.");
                } else if (entry->directory) {
                    snprintf(currentPath, sizeof(currentPath), "%s", selectedPath);
                    count = loadDirectory(entries, currentPath);
                    selected = 0;
                    snprintf(status, sizeof(status), "Choose a .win file.");
                } else if (!hasTextureBundle(selectedPath)) {
                    snprintf(status, sizeof(status), "Missing TEXTURES.BIN beside this WAD. Run the Donut Spider packer first.");
                } else {
                    char* result = safeStrdup(selectedPath);
                    free(entries);
                    return result;
                }
            }
        }

        char menu[8192];
        size_t used = 0;
        appendText(menu, sizeof(menu), &used,
            "DONUT SPIDER  %s\nSELECT WAD\n\nPath: %s\n%s\n\n",
            DONUT_SPIDER_VERSION,
            currentPath[0] != '\0' ? currentPath : "STORAGE", status);
        int32_t first = selected - PICKER_VISIBLE_ROWS / 2;
        if (first < 0) first = 0;
        if (first + PICKER_VISIBLE_ROWS > count) first = count - PICKER_VISIBLE_ROWS;
        if (first < 0) first = 0;
        int32_t last = first + PICKER_VISIBLE_ROWS;
        if (last > count) last = count;
        for (int32_t i = first; i < last; ++i) {
            char shortName[76];
            snprintf(shortName, sizeof(shortName), "%s", entries[i].name);
            appendText(menu, sizeof(menu), &used, "%s %s%s\n",
                i == selected ? ">" : " ", shortName, entries[i].directory && !entries[i].parent ? "/" : "");
        }
        if (count == 0) appendText(menu, sizeof(menu), &used, "  (No storage found)\n");
        appendText(menu, sizeof(menu), &used, "\nD-PAD: Move   X: Open/Select   O: Back   SQUARE: Refresh");
        PS3Overlay_drawTextScreen(menu, display_width, display_height);
        ps3glSwapBuffers();
        sysUtilCheckCallback();
        sysUsleep(16000);
    }
    free(entries);
    return nullptr;
}
