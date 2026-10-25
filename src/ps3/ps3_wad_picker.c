#include "ps3_wad_picker.h"
#include "picker_ui.h"
#include "picker_storage.h"
#include "ps3_stick.h"
#include "ps3_session_log.h"
#include "utils.h"
#include <ctype.h>
#include <dirent.h>
#include <io/pad.h>
#include <math.h>
#include <ps3gl.h>
#include <ppu_intrinsics.h>
#include "rsxutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#define PICKER_MAX_ENTRIES 512
typedef struct {
    char name[256];
    char rootPath[PICKER_PATH_MAX];
    bool directory,parent,directPath;
} PickerEntry;
typedef struct {
    PickerEntry entries[PICKER_MAX_ENTRIES];
    char roots[PICKER_MAX_ROOTS][128];
    size_t rootCount;
    char path[PICKER_PATH_MAX],pending[PICKER_PATH_MAX],appDirectory[PICKER_PATH_MAX];
    char message[128],status[160];
    bool home,confirming;
    int count,selected,footer,choice,homeSelected;
    PickerScan* scan;
} Picker;

static void addRoot(Picker* p,const char* path) {
    if (p->rootCount>=PICKER_MAX_ROOTS || strlen(path)>=sizeof(p->roots[0]) || !PickerStorage_isDirectory(path)) return;
    for (size_t i=0;i<p->rootCount;++i) if (!strcmp(p->roots[i],path)) return;
    snprintf(p->roots[p->rootCount++],sizeof(p->roots[0]),"%s",path);
}
static void collectRoots(Picker* p) {
    p->rootCount=0;
    for (unsigned i=0;i<8;++i) {char root[32];snprintf(root,sizeof(root),"/dev_usb%03u",i);addRoot(p,root);}
    addRoot(p,"/dev_hdd0");addRoot(p,"/dev_hdd1");addRoot(p,"/dev_bdvd");
    addRoot(p,"/dev_sd");addRoot(p,"/dev_ms");addRoot(p,"/dev_cf");addRoot(p,"/app_home");
    DIR* root=opendir("/");
    if (root) {
        struct dirent* entry;
        while ((entry=readdir(root))) {
            const char* n=entry->d_name;
            if (strncmp(n,"dev_usb",7) && strncmp(n,"dev_hdd",7) &&
                strcmp(n,"dev_sd") && strcmp(n,"dev_ms") && strcmp(n,"dev_cf")) continue;
            char path[128];int written=snprintf(path,sizeof(path),"/%s",n);
            if (written>0 && written<(int)sizeof(path)) addRoot(p,path);
        }
        closedir(root);
    }
    addRoot(p,p->appDirectory);
}
static void refresh(Picker* p) {
    PickerScan_destroy(p->scan);collectRoots(p);
    const char* roots[PICKER_MAX_ROOTS];
    for (size_t i=0;i<p->rootCount;++i) roots[i]=p->roots[i];
    p->scan=PickerScan_create(roots,p->rootCount);
    if (!p->scan) snprintf(p->message,sizeof(p->message),"Not enough memory to search storage.");
    PS3SessionLog_event("PICKER scan started: %u mounted locations",(unsigned)p->rootCount);
}
static int compareEntries(const void* a,const void* b) {
    const PickerEntry* x=a;const PickerEntry* y=b;
    if (x->directory!=y->directory) return x->directory?-1:1;
    const unsigned char* l=(const unsigned char*)x->name;const unsigned char* r=(const unsigned char*)y->name;
    while (*l && *r) {int d=tolower(*l++)-tolower(*r++);if (d) return d;}
    return (int)*l-(int)*r;
}
static void loadBrowser(Picker* p) {
    p->count=0;p->selected=0;p->footer=-1;p->confirming=false;
    if (!p->path[0]) {
        for (size_t i=0;i<p->rootCount;++i) {
            PickerEntry* e=&p->entries[p->count++];memset(e,0,sizeof(*e));
            snprintf(e->name,sizeof(e->name),"%s",p->roots[i]);
            snprintf(e->rootPath,sizeof(e->rootPath),"%s",p->roots[i]);e->directory=e->directPath=true;
        }
        snprintf(p->status,sizeof(p->status),p->count?"Choose a storage device.":"Connect a USB drive, then choose Refresh.");return;
    }
    PickerEntry* parent=&p->entries[p->count++];memset(parent,0,sizeof(*parent));
    strcpy(parent->name,"..  Back");parent->directory=parent->parent=true;
    DIR* dir=opendir(p->path);
    if (!dir) {snprintf(p->status,sizeof(p->status),"This storage folder is unavailable. Press O to go back.");return;}
    struct dirent* item;
    while ((item=readdir(dir)) && p->count<PICKER_MAX_ENTRIES) {
        if (!strcmp(item->d_name,".") || !strcmp(item->d_name,"..")) continue;
        char path[PICKER_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",p->path,item->d_name);
        if (n<0 || n>=(int)sizeof(path)) continue;
        bool directory=PickerStorage_isDirectory(path);
        if (!directory && !PickerStorage_isWad(item->d_name)) continue;
        PickerEntry* e=&p->entries[p->count++];memset(e,0,sizeof(*e));
        snprintf(e->name,sizeof(e->name),"%.255s",item->d_name);e->directory=directory;
        // Display names may be shortened; selection must retain the exact path.
        snprintf(e->rootPath,sizeof(e->rootPath),"%s",path);e->directPath=true;
    }
    closedir(dir);qsort(p->entries+1,(size_t)p->count-1,sizeof(PickerEntry),compareEntries);
    snprintf(p->status,sizeof(p->status),p->count==PICKER_MAX_ENTRIES?"Showing the first 511 items. Move other files to another folder.":"Choose a folder or a .win file.");
}
static void parentDirectory(char* path) {
    size_t len=strlen(path);while (len>1 && path[len-1]=='/') path[--len]='\0';
    char* slash=strrchr(path,'/');if (!slash || slash==path) path[0]='\0';else *slash='\0';
}
static void back(Picker* p,PickerUI* ui) {
    p->confirming=false;
    if (p->home) return;
    if (p->path[0]) {parentDirectory(p->path);loadBrowser(p);}
    else {p->home=true;p->selected=p->homeSelected;p->footer=p->selected>=7?p->selected-7:-1;}
    PickerUI_enter(ui);PickerUI_sound(ui,false);
}
static unsigned pollButtons(PS3PadState* controller) {
    static int port=-1;static bool stick[4];
    PS3Pad_poll(controller);
    if (controller->port!=port) {port=controller->port;memset(stick,0,sizeof(stick));}
    if (!controller->valid) return 0;
    const padData* data=&controller->data;
    unsigned d1=data->button[PAD_BUTTON_OFFSET_DIGITAL1],d2=data->button[PAD_BUTTON_OFFSET_DIGITAL2];
    unsigned x=data->button[PAD_BUTTON_OFFSET_ANALOG_LEFT_X],y=data->button[PAD_BUTTON_OFFSET_ANALOG_LEFT_Y];
    stick[0]=PS3Stick_direction((uint8_t)y,-1,stick[0]);stick[1]=PS3Stick_direction((uint8_t)y,1,stick[1]);
    stick[2]=PS3Stick_direction((uint8_t)x,-1,stick[2]);stick[3]=PS3Stick_direction((uint8_t)x,1,stick[3]);
    unsigned held=0;
    if ((d1&PAD_CTRL_UP) || stick[0]) held|=PICKER_UP;
    if ((d1&PAD_CTRL_DOWN) || stick[1]) held|=PICKER_DOWN;
    if ((d1&PAD_CTRL_LEFT) || stick[2]) held|=PICKER_LEFT;
    if ((d1&PAD_CTRL_RIGHT) || stick[3]) held|=PICKER_RIGHT;
    if (d2&PAD_CTRL_CROSS) held|=PICKER_ACCEPT;
    if (d2&PAD_CTRL_CIRCLE) held|=PICKER_BACK;
    if (d2&PAD_CTRL_SQUARE) held|=PICKER_RELOAD;
    return held;
}
static void move(Picker* p,PickerActions a,PickerUI* ui) {
    int before=p->selected,footer=p->footer,choice=p->choice;
    if (p->confirming) {if (a.horizontal) p->choice=1-p->choice;}
    else if (p->home) {
        if (a.vertical) p->selected=(p->selected+a.vertical+10)%10;
        if (a.horizontal && p->selected>=7) p->selected=7+(p->selected-7+a.horizontal+3)%3;
        p->footer=p->selected>=7?p->selected-7:-1;
    } else {
        if (p->footer>=0) {
            if (a.horizontal) p->footer=(p->footer+a.horizontal+3)%3;
            if (a.vertical) {p->footer=-1;p->selected=a.vertical<0?p->count-1:0;}
        } else if (a.vertical) {
            int next=p->selected+a.vertical;
            if (next<0 || next>=p->count) p->footer=0;else p->selected=next;
        }
    }
    if (before!=p->selected || footer!=p->footer || choice!=p->choice) {ui->age=0;PickerUI_sound(ui,false);}
}
static void accept(Picker* p,PickerUI* ui) {
    if (p->footer==0) {
        if (p->home) p->homeSelected=p->selected;
        p->home=false;p->path[0]='\0';loadBrowser(p);PickerUI_enter(ui);PickerUI_sound(ui,true);return;
    }
    if (p->footer==1) {refresh(p);if (!p->home) loadBrowser(p);PickerUI_sound(ui,true);return;}
    if (p->footer==2) {
        if (p->home) {strcpy(p->pending,"QUIT");PickerUI_launch(ui,0);}else {p->path[0]='\0';back(p,ui);}
        return;
    }
    if (p->home) {
        const PickerChapter* chapters=PickerScan_chapters(p->scan);
        if (p->selected>=PICKER_CHAPTERS) {snprintf(p->message,sizeof(p->message),"This chapter is not available.");return;}
        const PickerChapter* c=chapters?&chapters[p->selected]:NULL;
        if (!c || !c->path[0]) {snprintf(p->message,sizeof(p->message),"Chapter not found. Try Browse or Refresh.");return;}
        if (!c->ready || !PickerStorage_isFile(c->path) || !PickerStorage_hasTextures(c->path)) {
            snprintf(p->message,sizeof(p->message),"Missing data.win or TEXTURES.BIN. Refresh.");return;
        }
        if (!p->confirming) {p->confirming=true;p->choice=0;PickerUI_sound(ui,true);return;}
        if (p->choice) {p->confirming=false;PickerUI_sound(ui,true);return;}
        snprintf(p->pending,sizeof(p->pending),"%s",c->path);PickerUI_launch(ui,p->selected+1);return;
    }
    if (!p->count) return;
    const PickerEntry* entry=&p->entries[p->selected];
    if (entry->parent) {back(p,ui);return;}
    char path[PICKER_PATH_MAX];
    int written=entry->directPath?snprintf(path,sizeof(path),"%s",entry->rootPath):snprintf(path,sizeof(path),"%s/%s",p->path,entry->name);
    if (written<0 || written>=(int)sizeof(path)) {snprintf(p->message,sizeof(p->message),"That path is too long.");return;}
    if (entry->directory) {strcpy(p->path,path);loadBrowser(p);PickerUI_enter(ui);PickerUI_sound(ui,true);}
    else if (!PickerStorage_isFile(path) || !PickerStorage_hasTextures(path)) snprintf(p->message,sizeof(p->message),"Missing data.win or TEXTURES.BIN. Refresh.");
    else {strcpy(p->pending,path);PickerUI_launch(ui,PickerStorage_chapter(path));}
}
char* PS3WadPicker_select(const char* bundledWadPath,const bool* shouldExit,PS3PadState* controller) {
    Picker* p=calloc(1,sizeof(*p));if (!p) return NULL;
    snprintf(p->appDirectory,sizeof(p->appDirectory),"%s",bundledWadPath?bundledWadPath:"/app_home/data.win");parentDirectory(p->appDirectory);
    p->home=true;p->footer=-1;refresh(p);
    const PickerChapter* chapters=PickerScan_chapters(p->scan);
    if (chapters) for (int i=0;i<PICKER_CHAPTERS;++i) if (chapters[i].ready) {p->selected=i;break;}
    PickerUI ui;
    if (!PickerUI_init(&ui)) {
        PS3SessionLog_event("PICKER menu atlas allocation failed");PickerUI_deinit(&ui);PickerScan_destroy(p->scan);free(p);return NULL;
    }
    PickerInput input={0};uint64_t last=__builtin_ppc_get_timebase();
    double timeScale=1.0/(double)sysGetTimebaseFrequency();
    bool scanWasFinished=PickerScan_finished(p->scan);char* result=NULL;
    unsigned drawnFrames=0;bool pixelsLogged=false;
    while (!shouldExit || !*shouldExit) {
        PS3SessionLog_phase("chapter_select",-1,0);
        uint64_t now=__builtin_ppc_get_timebase();
        float dt=(float)((now-last)*timeScale);last=now;
        PickerUI_update(&ui,dt);PickerActions a=PickerInput_update(&input,pollButtons(controller),dt);
        if (!p->pending[0]) {
            PickerScan_step(p->scan,8);
            if (!scanWasFinished && PickerScan_finished(p->scan)) {
                scanWasFinished=true;unsigned found=0;chapters=PickerScan_chapters(p->scan);
                for (unsigned i=0;chapters && i<PICKER_CHAPTERS;++i) if (chapters[i].ready) ++found;
                PS3SessionLog_event("PICKER scan complete: %u ready chapters, %u paths beyond limits",found,PickerScan_skipped(p->scan));
            }
            if (p->message[0]) {if (a.accept || a.back) {p->message[0]='\0';PickerUI_sound(&ui,false);}}
            else {
                move(p,a,&ui);
                if (a.back) {
                    if (p->confirming) {p->confirming=false;PickerUI_sound(&ui,false);}
                    else if (!p->home) back(p,&ui);
                }
                if (a.reload) {refresh(p);scanWasFinished=PickerScan_finished(p->scan);if (!p->home) loadBrowser(p);PickerUI_sound(&ui,true);}
                else if (a.accept) {accept(p,&ui);scanWasFinished=PickerScan_finished(p->scan);}
            }
        }
        PickerView view={0};view.chapters=p->home;view.confirming=p->confirming;view.choice=p->choice;
        view.selected=p->selected;view.footer=p->footer;view.count=p->count;
        view.location=p->path[0]?p->path:"Storage";view.message=p->message;
        char scanStatus[160];
        if (p->home) {
            chapters=PickerScan_chapters(p->scan);
            for (int i=0;i<PICKER_CHAPTERS;++i) view.enabled[i]=chapters && chapters[i].ready;
            if (!PickerScan_finished(p->scan)) snprintf(scanStatus,sizeof(scanStatus),"Searching%s %s",((int)(ui.age*2)%3)==0?".":((int)(ui.age*2)%3)==1?"..":"...",PickerScan_location(p->scan));
            else snprintf(scanStatus,sizeof(scanStatus),"X: Select    O: Back    SQUARE: Search again");
            view.status=scanStatus;
        } else {
            view.first=p->selected/PICKER_ROWS*PICKER_ROWS;view.status=p->status;
            for (int row=0;row<PICKER_ROWS && view.first+row<p->count;++row) {
                view.names[row]=p->entries[view.first+row].name;view.directories[row]=p->entries[view.first+row].directory;
            }
        }
        PickerUI_draw(&ui,&view,display_width,display_height);
        if (++drawnFrames==1) {
            PS3SessionLog_event("PICKER first frame: display=%ux%u atlas=%u GL_error=0x%X",
                display_width,display_height,ui.texture,glGetError());
            ps3glLogGpuState();
        }
        if (!pixelsLogged && view.chapters && !view.confirming && ui.age>=3 && ui.slide==0 && ui.launchTime==0) {
            // Sample the opaque gray chapter separator, before flipping. This
            // distinguishes a black render buffer from a display/flip problem.
            float scale=fminf(display_width/640.0f,display_height/480.0f);
            int x=(display_width-(int)(640*scale))/2+(int)(200.5f*scale);
            int y=(int)display_height-1-(int)(62.5f*scale);
            unsigned char rgba[4]={0};
            glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
            PS3SessionLog_event("PICKER pixels: frame=%u alpha_milli=%u separator_RGBA=%u,%u,%u,%u GL_error=0x%X scan_finished=%u",
                drawnFrames,(unsigned)(ui.alpha*1000),rgba[0],rgba[1],rgba[2],rgba[3],glGetError(),PickerScan_finished(p->scan));
            pixelsLogged=true;
        }
        ps3glSwapBuffers();sysUtilCheckCallback();
        if (p->pending[0] && PickerUI_launchFinished(&ui)) {
            if (strcmp(p->pending,"QUIT")) result=safeStrdup(p->pending);
            break;
        }
        sysUsleep(1000);
    }
    PickerUI_deinit(&ui);PickerScan_destroy(p->scan);free(p);
    if (result) PS3SessionLog_phase("loading",-1,0);
    return result;
}
