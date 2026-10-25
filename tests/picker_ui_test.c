#include "picker_ui.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PICKER_UI_SOUND
#include <SDL2/SDL.h>
#endif
#ifdef PICKER_PREVIEW
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

static void pixel(int x,int y,unsigned char* p) {glReadPixels(x,479-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);}
#ifdef PICKER_PREVIEW
static void capture(const char* folder,const char* name,int w,int h) {
    unsigned char* p=malloc((size_t)w*h*4);assert(p);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p);
    stbi_flip_vertically_on_write(1);char path[2048];snprintf(path,sizeof(path),"%s/%s.png",folder,name);
    assert(stbi_write_png(path,w,h,4,p,w*4));free(p);
}
#endif
int main(int argc,char** argv) {
    (void)argc;(void)argv;
    PickerInput input={0};PickerActions a=PickerInput_update(&input,PICKER_DOWN|PICKER_ACCEPT,0.016f);
    assert(a.vertical==1 && a.accept);
    unsigned repeats=0;
    for (int i=0;i<60;++i) {a=PickerInput_update(&input,PICKER_DOWN|PICKER_ACCEPT,1.0f/60);assert(!a.accept);if (a.vertical) ++repeats;}
    assert(repeats>=8 && repeats<=9);
    a=PickerInput_update(&input,0,0.016f);assert(!a.vertical);
    a=PickerInput_update(&input,PICKER_ACCEPT,0.016f);assert(a.accept);
    a=PickerInput_update(&input,PICKER_UP|PICKER_DOWN,0.016f);assert(!a.vertical);
    assert(glfwInit());glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,2);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    GLFWwindow* window=glfwCreateWindow(1280,720,"Picker QA",NULL,NULL);assert(window);glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));PickerUI ui;assert(PickerUI_init(&ui));
#ifdef PICKER_UI_SOUND
    assert(ui.audioDevice);
    PickerUI_sound(&ui,false);PickerUI_sound(&ui,true);
    for (int chapter=1;chapter<=5;++chapter) {
        PickerUI_launch(&ui,chapter);SDL_Delay(40);
        SDL_LockAudioDevice(ui.audioDevice);
        assert(ui.voice && ui.voicePosition>0 && ui.voicePosition<ui.voiceLength);
        SDL_UnlockAudioDevice(ui.audioDevice);
    }
    PickerUI_enter(&ui);
#endif
    PickerUI thirty=ui,sixty=ui;
    for (unsigned i=0;i<30;++i) PickerUI_update(&thirty,1.0f/30);
    for (unsigned i=0;i<60;++i) PickerUI_update(&sixty,1.0f/60);
    assert(fabsf(thirty.alpha-sixty.alpha)<0.0001f && fabsf(thirty.slide-sixty.slide)<0.0001f);
    assert(thirty.alpha>0.84f && thirty.alpha<0.85f);
    PickerView view={0};view.chapters=true;view.footer=-1;view.status="X: Select    O: Back    SQUARE: Search again";
    for (int i=0;i<5;++i) view.enabled[i]=true;
#ifdef PICKER_PREVIEW
    assert(argc==2);
    for (int frame=0;frame<150;++frame) {
        PickerUI_update(&ui,1.0f/30);PickerUI_draw(&ui,&view,640,480);
        if (frame==4 || frame==14 || frame==29 || frame==59) {char name[64];snprintf(name,sizeof(name),"picker-frame-%03d",frame+1);capture(argv[1],name,640,480);}
    }
#endif
    // A caller's disabled write mask and rejecting alpha test must not blank
    // the menu. The picker owns and establishes these states explicitly.
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_NEVER,1);
    ui.alpha=1;ui.slide=0;PickerUI_draw(&ui,&view,640,480);glFinish();unsigned char p[4];
    pixel(21,28,p);assert(p[0]>250 && p[1]<5 && p[2]<5); // red SOUL
    pixel(200,62,p);assert(p[0]==43 && p[1]==43 && p[2]==43); // separator
    pixel(0,0,p);assert(p[0]==0 && p[1]==0 && p[2]==0);
#ifdef PICKER_PREVIEW
    capture(argv[1],"picker-chapters",640,480);
    view.selected=1;view.confirming=true;view.choice=0;PickerUI_draw(&ui,&view,640,480);capture(argv[1],"picker-play",640,480);
    view.choice=1;PickerUI_draw(&ui,&view,640,480);capture(argv[1],"picker-do-not",640,480);
    view.confirming=false;view.enabled[1]=false;view.message="Missing data.win or TEXTURES.BIN. Refresh.";
    PickerUI_draw(&ui,&view,640,480);capture(argv[1],"picker-missing",640,480);view.message=NULL;
    view.chapters=false;view.selected=1;view.count=4;view.location="/dev_usb000/DELTARUNE";view.status="Choose a folder or a .win file.";
    view.names[0]="..  Back";view.names[1]="chapter1_windows";view.names[2]="chapter2_windows";view.names[3]="chapter3_windows";
    for (int i=0;i<4;++i) view.directories[i]=true;
    PickerUI_draw(&ui,&view,640,480);capture(argv[1],"picker-browse",640,480);
    view.chapters=true;view.count=0;view.selected=1;view.enabled[1]=true;view.status="X: Select    O: Back    SQUARE: Search again";
    PickerUI_draw(&ui,&view,1280,720);capture(argv[1],"picker-widescreen",1280,720);
#endif
    PickerUI_launch(&ui,2);for (int i=0;i<30;++i) PickerUI_update(&ui,1.0f/60);
    assert(!PickerUI_launchFinished(&ui) && fabsf(ui.fade-powf(0.875f,15))<0.001f);
#ifdef PICKER_PREVIEW
    PickerUI_draw(&ui,&view,640,480);capture(argv[1],"picker-launch",640,480);
#endif
    for (int i=0;i<60;++i) PickerUI_update(&ui,1.0f/60);
    assert(PickerUI_launchFinished(&ui) && ui.fade<0.003f);
    // The 4:3 canvas is 960 pixels wide at 720p, with black 160-pixel sidebars.
    PickerUI_enter(&ui);ui.alpha=1;ui.slide=0;view.chapters=true;view.selected=0;view.footer=-1;
    PickerUI_draw(&ui,&view,1280,720);glReadPixels(20,360,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(!p[0] && !p[1] && !p[2]);
    glReadPixels(160+300,719-93,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);assert(p[0]==43 && p[1]==43 && p[2]==43);
    PickerUI_enter(&ui);assert(!PickerUI_launchFinished(&ui));
    assert(glGetError()==GL_NO_ERROR);PickerUI_deinit(&ui);glfwDestroyWindow(window);glfwTerminate();
    puts("PASS: controller repeat/edges, refresh-independent animation, atlas/heart rendering, aspect ratio and launch fade");
}
