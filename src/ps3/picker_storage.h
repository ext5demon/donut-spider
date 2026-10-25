#ifndef PICKER_STORAGE_H
#define PICKER_STORAGE_H
#include <stdbool.h>
#include <stddef.h>

#define PICKER_PATH_MAX 1024
#define PICKER_CHAPTERS 5
#define PICKER_MAX_ROOTS 32
typedef struct {
    char path[PICKER_PATH_MAX];
    bool ready;
} PickerChapter;
typedef struct PickerScan PickerScan;

bool PickerStorage_isDirectory(const char* path);
bool PickerStorage_isFile(const char* path);
bool PickerStorage_isWad(const char* name);
bool PickerStorage_hasTextures(const char* wad);
// Read the bounded GEN8 title, never infer chapter identity from a folder name.
int PickerStorage_chapter(const char* wad);
PickerScan* PickerScan_create(const char* const* roots, size_t count);
void PickerScan_destroy(PickerScan* scan);
// Incremental depth-first traversal: at most budget directory entries per call.
void PickerScan_step(PickerScan* scan, unsigned budget);
bool PickerScan_finished(const PickerScan* scan);
const PickerChapter* PickerScan_chapters(const PickerScan* scan);
const char* PickerScan_location(const PickerScan* scan);
unsigned PickerScan_skipped(const PickerScan* scan);
#endif
