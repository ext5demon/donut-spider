#include "picker_ui.h"
#if defined(PLATFORM_PS3)
#include <ps3gl.h>
#define PICKER_UI_SOUND
#else
#include <glad/glad.h>
#endif
#ifdef PICKER_UI_SOUND
#include <SDL2/SDL.h>
#endif
#include "picker_assets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PICKER_UI_SOUND
static void menuAudio(void* userdata,Uint8* stream,int bytes) {
    PickerUI* ui=userdata;int16_t* out=(int16_t*)stream;memset(stream,0,(size_t)bytes);
    for (int i=0;i<bytes/4 && ui->voice;++i) {
        size_t at=(size_t)ui->voicePosition;
        if (at>=ui->voiceLength) {ui->voice=NULL;break;}
        int16_t sample=(int16_t)(ui->voice[at]*ui->voiceGain);
        out[i*2]=out[i*2+1]=sample;ui->voicePosition+=44100.0/48000.0;
    }
}
#endif
void PickerUI_sound(PickerUI* ui,bool select) {
#ifdef PICKER_UI_SOUND
    if (!ui->audioDevice) return;
    SDL_LockAudioDevice(ui->audioDevice);
    ui->voice=select?pickerSound_select:pickerSound_move;
    ui->voiceLength=select?sizeof(pickerSound_select)/sizeof(int16_t):sizeof(pickerSound_move)/sizeof(int16_t);
    ui->voicePosition=0;ui->voiceGain=1;SDL_UnlockAudioDevice(ui->audioDevice);
#else
    (void)ui;(void)select;
#endif
}
bool PickerUI_init(PickerUI* ui) {
    memset(ui,0,sizeof(*ui));ui->slide=-40;ui->fade=1;
    static const unsigned char colors[4][4]={{255,255,255,0},{255,255,255,255},{0,0,0,255},{255,0,0,255}};
    unsigned char* rgba=malloc(PICKER_ATLAS_W*PICKER_ATLAS_H*4);if (!rgba) return false;
    for (unsigned i=0;i<PICKER_ATLAS_W*PICKER_ATLAS_H;++i)
        memcpy(rgba+4*i,colors[(pickerAtlasPacked[i/4]>>(6-2*(i%4)))&3],4);
    glGenTextures(1,&ui->texture);glBindTexture(GL_TEXTURE_2D,ui->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,PICKER_ATLAS_W,PICKER_ATLAS_H,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);free(rgba);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);
#ifdef PICKER_UI_SOUND
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
        ui->audioInitialized=true;SDL_AudioSpec spec={0};
        spec.freq=48000;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=512;
        spec.callback=menuAudio;spec.userdata=ui;
        ui->audioDevice=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (ui->audioDevice) SDL_PauseAudioDevice(ui->audioDevice,0);
    }
#endif
    return ui->texture!=0;
}
void PickerUI_deinit(PickerUI* ui) {
#ifdef PICKER_UI_SOUND
    if (ui->audioDevice) SDL_CloseAudioDevice(ui->audioDevice);
    if (ui->audioInitialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
    if (ui->texture) glDeleteTextures(1,&ui->texture);
    memset(ui,0,sizeof(*ui));
}
void PickerUI_enter(PickerUI* ui) { ui->alpha=0;ui->slide=-40;ui->age=0;ui->fade=1;ui->launchTime=0; }
void PickerUI_update(PickerUI* ui,float seconds) {
    if (!isfinite(seconds) || seconds<=0) return;
    if (seconds>0.1f) seconds=0.1f;
    ui->age+=seconds;
    // The launcher runs at 30 Hz: alpha lerp .06, y lerp .14 from ystart-40.
    ui->alpha=1-(1-ui->alpha)*powf(0.94f,seconds*30);
    ui->slide*=powf(0.86f,seconds*30);
    if (ui->alpha>0.999f) ui->alpha=1;
    if (fabsf(ui->slide)<0.1f) ui->slide=0;
    if (ui->launchTime>0) {
        ui->launchTime+=seconds;ui->fade=powf(0.875f,ui->launchTime*30);
    }
}
void PickerUI_launch(PickerUI* ui,int chapter) {
    static const unsigned frames[]={PICKER_CHAPTER1_FRAMES,PICKER_CHAPTER2_FRAMES,PICKER_CHAPTER3_FRAMES,PICKER_CHAPTER4_FRAMES,PICKER_CHAPTER5_FRAMES};
    ui->launchTime=0.0001f;
    ui->launchDuration=chapter>=1 && chapter<=5?roundf(frames[chapter-1]/44100.0f*30)/30:0.5f;
#ifdef PICKER_UI_SOUND
    if (chapter>=1 && chapter<=5 && ui->audioDevice) {
        static const int16_t* const voices[]={pickerSound_chapter1,pickerSound_chapter2,pickerSound_chapter3,pickerSound_chapter4,pickerSound_chapter5};
        SDL_LockAudioDevice(ui->audioDevice);ui->voice=voices[chapter-1];ui->voiceLength=frames[chapter-1];
        ui->voicePosition=0;ui->voiceGain=chapter==3?0.7f:1;SDL_UnlockAudioDevice(ui->audioDevice);
    } else PickerUI_sound(ui,true);
#else
    (void)chapter;
#endif
}
bool PickerUI_launchFinished(const PickerUI* ui) {return ui->launchTime>0 && ui->launchTime>=ui->launchDuration;}

PickerActions PickerInput_update(PickerInput* input,unsigned held,float seconds) {
    PickerActions a={0};unsigned edges=held&~input->held;
    a.accept=(edges&PICKER_ACCEPT)!=0;a.back=(edges&PICKER_BACK)!=0;a.reload=(edges&PICKER_RELOAD)!=0;
    int vertical=((held&PICKER_DOWN)!=0)-((held&PICKER_UP)!=0);
    int horizontal=((held&PICKER_RIGHT)!=0)-((held&PICKER_LEFT)!=0);
    int direction=vertical?vertical:horizontal*2;
    bool move=false;
    if (direction!=input->direction) {move=direction!=0;input->repeat=0.35f;}
    else if (direction && seconds>0 && isfinite(seconds)) {
        input->repeat-=fminf(seconds,0.1f);
        if (input->repeat<=0) {move=true;input->repeat+=0.08f;}
    }
    if (move) {a.vertical=vertical;a.horizontal=vertical?0:horizontal;}
    input->direction=direction;input->held=held;return a;
}
static void color(unsigned rgb,float alpha) {
    glColor4f(((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,alpha);
}
static void quad(float x,float y,float w,float h,float u,float v,float uw,float vh) {
    glBegin(GL_QUADS);
    glTexCoord2f(u,v);glVertex2f(x,y);
    glTexCoord2f(u+uw,v);glVertex2f(x+w,y);
    glTexCoord2f(u+uw,v+vh);glVertex2f(x+w,y+h);
    glTexCoord2f(u,v+vh);glVertex2f(x,y+h);glEnd();
}
static void sprite(const PickerSprite* s,float x,float y,float scale) {
    quad(x,y,s->w*scale,s->h*scale,(float)s->x/PICKER_ATLAS_W,(float)s->y/PICKER_ATLAS_H,
         (float)s->w/PICKER_ATLAS_W,(float)s->h/PICKER_ATLAS_H);
}
static const PickerGlyph* nextGlyph(const char** text,bool small) {
    unsigned char c=(unsigned char)*(*text)++;
    if (c>=128) {while (((unsigned char)**text&0xc0)==0x80) ++*text;c='?';}
    if (c<32 || c>126) c='?';
    return &(small?picker_fnt_small:picker_fnt_main)[c-32];
}
static float textWidth(const char* text,bool small,float scale) {
    float width=0;if (text) while (*text) width+=nextGlyph(&text,small)->advance*scale;
    return width;
}
static void text(float x,float y,const char* value,bool small,float scale,float maxWidth) {
    if (!value) return;
    float start=x;
    while (*value) {
        const PickerGlyph* g=nextGlyph(&value,small);
        if (x+g->w*scale>start+maxWidth) break;
        if (g->w && g->h) quad(x+g->offset*scale,y,g->w*scale,g->h*scale,
            (float)g->x/PICKER_ATLAS_W,(float)g->y/PICKER_ATLAS_H,(float)g->w/PICKER_ATLAS_W,(float)g->h/PICKER_ATLAS_H);
        x+=g->advance*scale;
    }
}
static void rect(float x,float y,float w,float h,unsigned rgb,float alpha) {
    glDisable(GL_TEXTURE_2D);color(rgb,alpha);quad(x,y,w,h,0,0,0,0);glEnable(GL_TEXTURE_2D);
}
static void choice(float x,float y,const char* label,bool selected,float alpha) {
    color(selected?0xffff00:0xffffff,alpha);text(x+30,y,label,false,2,580-x);
    if (selected) {color(0xffffff,alpha);sprite(&picker_heart[0],x,y+8,2);}
}
void PickerUI_draw(const PickerUI* ui,const PickerView* v,int width,int height) {
    if (width<=0 || height<=0) return;
    // The picker owns the default framebuffer and fixed-function pipeline.
    // Do not inherit write masks, alpha tests or shader/texture units from a probe.
    glBindFramebuffer(GL_FRAMEBUFFER,0);glUseProgram(0);glActiveTexture(GL_TEXTURE0);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_ALPHA_TEST);
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    // Keep the game's 4:3 canvas at every PS3 video mode.
    float scale=fminf(width/640.0f,height/480.0f);
    int w=(int)(640*scale),h=(int)(480*scale);glViewport((width-w)/2,(height-h)/2,w,h);
    const float projection[16]={2.0f/640,0,0,0, 0,-2.0f/480,0,0, 0,0,-1,0, -1,1,0,1};
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadMatrixf(projection);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    if (ui->launchTime>0) {
        // Original screen_transition: xscale *= .95, yscale/yy *= .99 per 30 Hz step.
        glTranslatef(320,0,0);glScalef(powf(0.95f,ui->launchTime*30),powf(0.99f,ui->launchTime*30),1);glTranslatef(-320,0,0);
    }
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_FOG);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glBindTexture(GL_TEXTURE_2D,ui->texture);
    float a=ui->alpha*ui->fade,slide=roundf(ui->slide);
    if (v->chapters) {
        static const char* titles[]={"The Beginning","A Cyber's World","Late Night","Prophecy","Festival Day","- -","- -"};
        static const int icons[]={1,2,3,5,6,0,0};
        for (int i=0;i<7;++i) {
            float y=16+60*i+slide;char label[24];snprintf(label,sizeof(label),"Chapter %d",i+1);
            bool selected=v->selected==i;unsigned rgb=!v->enabled[i]?0x808080:selected?0xffff00:0xffffff;
            color(rgb,a);text(50,y,label,false,2,170);
            if (v->confirming && selected) {
                choice(220,y,"Play",v->choice==0,a);choice(400,y,"Do Not",v->choice==1,a);
            } else {
                color(rgb,a);text(360-textWidth(titles[i],false,2)/2,y,titles[i],false,2,300);
                if (selected) {color(0xffffff,a);sprite(&picker_heart[0],20,y+8,2);}
            }
            color(rgb,a);sprite(&picker_icons[icons[i]],553,10+60*i+slide,2);
            rect(0,62+60*i+slide,640,2,0x2b2b2b,a);
        }
    } else {
        color(0xffffff,a);text(20,12+slide,"CHOOSE GAME",false,2,600);
        color(0x808080,a);text(20,48+slide,v->location,true,2,600);
        rect(0,70+slide,640,2,0x2b2b2b,a);
        for (int row=0;row<PICKER_ROWS;++row) {
            int i=v->first+row;if (i>=v->count) break;
            float y=82+46*row+slide;bool selected=i==v->selected && v->footer<0;
            color(selected?0xffff00:0xffffff,a);text(50,y,v->names[row],false,2,500);
            if (selected) {color(0xffffff,a);sprite(&picker_heart[0],20,y+8,2);}
            if (v->directories[row]) {color(0x808080,a);text(566,y+8,"DIR",true,2,60);}
            rect(0,y+36,640,2,0x2b2b2b,a);
        }
        if (!v->count) {color(0x808080,a);text(50,104+slide,"No storage found",false,2,560);}
        color(0x808080,a);text(20,410,v->status,true,1,600);
    }
    color(0x808080,a);text(16,430,"Spider Donut",false,1,164);text(16,446,DONUT_SPIDER_VERSION,false,1,164);
    choice(180,432,"Browse",v->footer==0,a);choice(335,432,"Refresh",v->footer==1,a);choice(510,432,v->chapters?"Quit":"Back",v->footer==2,a);
    color(0x808080,a);text(16,470,v->chapters?v->status:"X: Open    O: Back    SQUARE: Refresh",true,1,608);
    if (v->message && *v->message) {
        rect(0,0,640,480,0,0.75f);rect(48,152,544,168,0xffffff,1);rect(52,156,536,160,0,1);
        color(0xffffff,1);text(80,178,"Cannot open this chapter",false,2,500);
        text(80,232,v->message,true,2,480);choice(268,268,"Back",true,1);
    }
    glBindTexture(GL_TEXTURE_2D,0);glColor4f(1,1,1,1);
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
    glDisable(GL_SCISSOR_TEST);glViewport(0,0,width,height);
}
