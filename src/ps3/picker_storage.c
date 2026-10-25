#include "picker_storage.h"
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SCAN_DEPTH 48
typedef struct { DIR* dir; char path[PICKER_PATH_MAX]; } ScanLevel;
struct PickerScan {
    PickerChapter chapters[PICKER_CHAPTERS];
    char roots[PICKER_MAX_ROOTS][PICKER_PATH_MAX];
    size_t rootCount, nextRoot;
    ScanLevel levels[SCAN_DEPTH];
    unsigned depth, skipped;
    bool finished;
};

bool PickerStorage_isDirectory(const char* path) {
    struct stat st;
    return path && stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
bool PickerStorage_isFile(const char* path) {
    struct stat st;
    return path && stat(path,&st)==0 && S_ISREG(st.st_mode);
}
bool PickerStorage_isWad(const char* name) {
    size_t n=name?strlen(name):0;
    return n>=4 && name[n-4]=='.' && tolower((unsigned char)name[n-3])=='w' &&
        tolower((unsigned char)name[n-2])=='i' && tolower((unsigned char)name[n-1])=='n';
}
bool PickerStorage_hasTextures(const char* wad) {
    char path[PICKER_PATH_MAX];
    if (!wad || strlen(wad)>=sizeof(path)) return false;
    strcpy(path,wad);char* slash=strrchr(path,'/');
    if (!slash) return false;
    size_t left=sizeof(path)-(size_t)(slash+1-path);
    if (left<sizeof("TEXTURES.BIN")) return false;
    strcpy(slash+1,"TEXTURES.BIN");
    if (PickerStorage_isFile(path)) return true;
    strcpy(slash+1,"textures.bin");return PickerStorage_isFile(path);
}
static uint32_t little32(const unsigned char* b) {
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
int PickerStorage_chapter(const char* wad) {
    FILE* f=fopen(wad,"rb");if (!f) return 0;
    unsigned char h[104];int result=0;
    if (fread(h,1,8,f)!=8 || memcmp(h,"FORM",4)) goto done;
    uint32_t formSize=little32(h+4);long position=8;
    // GEN8 is normally first. Limit malformed files and unrelated .win files.
    for (unsigned chunk=0;chunk<64 && position<1024*1024;++chunk) {
        if (fseek(f,position,SEEK_SET) || fread(h,1,8,f)!=8) break;
        uint32_t size=little32(h+4);
        if (size>formSize || (uint64_t)position+size>UINT32_MAX-8) break;
        if (!memcmp(h,"GEN8",4)) {
            if (size<104 || fread(h,1,104,f)!=104 || h[1]<13) break;
            uint32_t title=little32(h+100);
            if (title<8 || title>formSize || formSize-title<20) break;
            char text[96]={0};
            if (fseek(f,(long)title,SEEK_SET) || fread(text,1,sizeof(text)-1,f)<20) break;
            const char* prefix="deltarune chapter ";size_t i=0;
            while (prefix[i] && tolower((unsigned char)text[i])==prefix[i]) ++i;
            if (!prefix[i] && text[i]>='1' && text[i]<='5' &&
                (text[i+1]=='\0' || text[i+1]==' ' || text[i+1]=='-' || text[i+1]=='(')) result=text[i]-'0';
            break;
        }
        position+=8+(long)size;
    }
done:
    fclose(f);return result;
}
static void consider(PickerScan* scan,const char* path) {
    int chapter=PickerStorage_chapter(path);if (!chapter) return;
    PickerChapter* c=&scan->chapters[chapter-1];bool ready=PickerStorage_hasTextures(path);
    // Prefer a complete copy; otherwise preserve deterministic root priority.
    if (!c->path[0] || (!c->ready && ready)) {
        snprintf(c->path,sizeof(c->path),"%s",path);c->ready=ready;
    }
}
static bool allReady(const PickerScan* scan) {
    for (unsigned i=0;i<PICKER_CHAPTERS;++i) if (!scan->chapters[i].ready) return false;
    return true;
}
static void closeLevels(PickerScan* scan) {
    while (scan->depth) closedir(scan->levels[--scan->depth].dir);
}
PickerScan* PickerScan_create(const char* const* roots,size_t count) {
    PickerScan* scan=calloc(1,sizeof(*scan));if (!scan) return NULL;
    for (size_t r=0;r<count && scan->rootCount<PICKER_MAX_ROOTS;++r) {
        if (!roots[r] || strlen(roots[r])>=PICKER_PATH_MAX || !PickerStorage_isDirectory(roots[r])) continue;
        bool duplicate=false;
        for (size_t i=0;i<scan->rootCount;++i) if (!strcmp(scan->roots[i],roots[r])) duplicate=true;
        if (duplicate) continue;
        snprintf(scan->roots[scan->rootCount++],PICKER_PATH_MAX,"%s",roots[r]);
        const char* direct[]={"data.win","game.win"};
        for (unsigned i=0;i<2;++i) {
            char path[PICKER_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",roots[r],direct[i]);
            if (n>0 && n<(int)sizeof(path) && PickerStorage_isFile(path)) consider(scan,path);
        }
        const char* prefixes[]={"","/DELTARUNE","/GAMES/DELTARUNE","/game/DELTARUNE"};
        for (unsigned p=0;p<sizeof(prefixes)/sizeof(prefixes[0]);++p) {
            for (unsigned c=1;c<=PICKER_CHAPTERS;++c) {
                char path[PICKER_PATH_MAX];
                int n=snprintf(path,sizeof(path),"%s%s/chapter%u_windows/data.win",roots[r],prefixes[p],c);
                if (n>0 && n<(int)sizeof(path) && PickerStorage_isFile(path)) consider(scan,path);
            }
        }
    }
    scan->finished=allReady(scan) || scan->rootCount==0;return scan;
}
void PickerScan_destroy(PickerScan* scan) { if (scan) {closeLevels(scan);free(scan);} }
void PickerScan_step(PickerScan* scan,unsigned budget) {
    if (!scan || scan->finished) return;
    while (budget--) {
        if (!scan->depth) {
            if (scan->nextRoot==scan->rootCount) {scan->finished=true;break;}
            ScanLevel* level=&scan->levels[0];
            snprintf(level->path,sizeof(level->path),"%s",scan->roots[scan->nextRoot++]);
            level->dir=opendir(level->path);if (level->dir) scan->depth=1;continue;
        }
        ScanLevel* level=&scan->levels[scan->depth-1];struct dirent* item=readdir(level->dir);
        if (!item) {closedir(level->dir);--scan->depth;continue;}
        if (!strcmp(item->d_name,".") || !strcmp(item->d_name,"..")) continue;
        char path[PICKER_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",level->path,item->d_name);
        if (n<0 || n>=(int)sizeof(path)) {++scan->skipped;continue;}
        struct stat st;
#ifdef PLATFORM_PS3
        if (stat(path,&st)) continue; // PS3 storage has no POSIX symlinks.
#else
        if (lstat(path,&st)) continue; // Do not follow cycles or leave a storage root.
#endif
        if (S_ISDIR(st.st_mode)) {
            if (scan->depth==SCAN_DEPTH) {++scan->skipped;continue;}
            DIR* dir=opendir(path);if (!dir) continue;
            ScanLevel* next=&scan->levels[scan->depth++];next->dir=dir;strcpy(next->path,path);
        } else if (S_ISREG(st.st_mode) && PickerStorage_isWad(item->d_name)) consider(scan,path);
        if (allReady(scan)) {scan->finished=true;closeLevels(scan);break;}
    }
}
bool PickerScan_finished(const PickerScan* scan) { return !scan || scan->finished; }
const PickerChapter* PickerScan_chapters(const PickerScan* scan) { return scan?scan->chapters:NULL; }
const char* PickerScan_location(const PickerScan* scan) {
    return scan && scan->depth?scan->levels[scan->depth-1].path:"Storage";
}
unsigned PickerScan_skipped(const PickerScan* scan) { return scan?scan->skipped:0; }
