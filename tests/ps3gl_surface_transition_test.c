/* Run the production PS3GL API against an RSX call recorder. This validates
 * submitted bounds and resource transitions, not physical GPU rendering. */
#include "../vendor/ps3gl/ps3gl.c"
#include "../vendor/ps3gl/raster_bounds.h"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include "../src/ps3/picker_ui.h"

u32 display_width = 1280, display_height = 720, curr_fb;
u32 color_pitch, color_offset[2], *color_buffer[2], depth_pitch, depth_offset;
static u32 commands[1024], labels[256];
static gcmContextData testContext;
gcmContextData* context = &testContext;
static gcmSurface hardwareTarget;
static Ps3glRasterRect hardwareViewport, hardwareScissor;
static float hardwareScale[4], hardwareOffset[4];
static unsigned viewportCalls, textureCalls, clears, draws, liveAllocations;
static unsigned readbackCalls;
static unsigned vertexProgramLoads,fragmentProgramLoads,scalarUniformWrites;
static u32 hardwareColorMask,hardwareDepthWrite;
static bool pickerRecording,modelUploaded,projectionUploaded;
static unsigned pickerTextured,pickerSolid;
static float hardwareModel[16],hardwareProjection[16];
static bool hardwareTextureEnabled;
void rsxSetColorMask(gcmContextData* ctx,u32 mask){(void)ctx;hardwareColorMask=mask;}
void rsxSetDepthWriteEnable(gcmContextData* ctx,u32 enable){(void)ctx;hardwareDepthWrite=enable;}
void rsxLoadVertexProgram(gcmContextData* ctx,rsxVertexProgram* program,const void* code){(void)ctx;(void)program;(void)code;++vertexProgramLoads;modelUploaded=projectionUploaded=false;}
void rsxSetVertexProgramParameter(gcmContextData* ctx,rsxVertexProgram* program,rsxProgramConst* param,const float* value) {
    (void)ctx;(void)program;
    if(param==_opengl_state.prog_consts[PS3GL_Uniform_ModelViewMatrix]){memcpy(hardwareModel,value,64);modelUploaded=true;}
    if(param==_opengl_state.prog_consts[PS3GL_Uniform_ProjectionMatrix]){memcpy(hardwareProjection,value,64);projectionUploaded=true;}
}
void rsxLoadFragmentProgramLocation(gcmContextData* ctx,rsxFragmentProgram* program,u32 offset,u8 location){(void)ctx;(void)program;(void)offset;(void)location;++fragmentProgramLoads;}
void rsxInlineTransfer(gcmContextData* ctx,u32 offset,const void* value,u32 count,u32 location){
    (void)ctx;(void)location;if(count==1)++scalarUniformWrites;
    if(offset==fp_offset+PS3GL_Uniform_TextureEnabled*16 && count==1){float f;memcpy(&f,value,4);hardwareTextureEnabled=swapF32_16(f)!=0;}
}
static unsigned char readbackScratch[PS3GL_READBACK_CAPACITY];
const unsigned char* rsxReadbackTransfer(u32 source,u32 pitch,u32 rowBytes,u32 rows) {
    assert(rowBytes*rows<=sizeof(readbackScratch));
    ++readbackCalls;
    for(unsigned i=1;i<MAX_TEXTURES;++i) {
        struct ps3gl_texture* tx=&_opengl_state.textures[i];
        u32 delta=source-tx->gcmTexture.offset;
        if(tx->data&&delta<(u32)tx->gcmTexture.pitch*tx->gcmTexture.height) {
            assert((uint64_t)delta+(rows-1)*pitch+rowBytes<=(uint64_t)tx->gcmTexture.pitch*tx->gcmTexture.height);
            for(unsigned y=0;y<rows;++y)memcpy(readbackScratch+y*rowBytes,tx->data+delta+y*pitch,rowBytes);
            return readbackScratch;
        }
    }
    assert(!"invalid readback source");return NULL;
}

void PS3SessionLog_event(const char* format, ...) {(void)format;}
void PS3SessionLog_trace(const char* format, ...) {(void)format;}
void PS3SessionLog_flushToDisk(void) {}
void PS3SessionLog_close(const char* reason) {(void)reason;}
void init_screen(void* address, u32 size) {(void)address;(void)size;}
void flip(void) {curr_fb ^= 1;setRenderTarget(curr_fb);}
void setRenderTarget(u32 index) {
    (void)index;
    hardwareTarget = (gcmSurface){.width=display_width,.height=display_height};
}
void* rsxMemalign(size_t alignment, size_t size) {
    void* p = NULL; assert(posix_memalign(&p, alignment, size) == 0);
    ++liveAllocations;return p;
}
void rsxFree(void* p) {if(p){assert(liveAllocations);--liveAllocations;free(p);}}
void rsxAddressToOffset(const void* p, u32* offset) {*offset = (u32)(uintptr_t)p;}
void rsxSetWriteBackendLabel(gcmContextData* ctx, u8 index, u32 value) {(void)ctx;labels[index]=value;}
void* gcmGetLabelAddress(u8 index) {return &labels[index];}
void rsxSetSurface(gcmContextData* ctx, const gcmSurface* surface) {
    (void)ctx;assert(surface->width && surface->height);hardwareTarget=*surface;
}
static void assertBounds(Ps3glRasterRect r) {
    assert(r.x <= hardwareTarget.width && r.y <= hardwareTarget.height);
    assert((unsigned)r.x+r.w <= hardwareTarget.width);
    assert((unsigned)r.y+r.h <= hardwareTarget.height);
}
void rsxSetViewport(gcmContextData* ctx, u16 x, u16 y, u16 w, u16 h,
                    f32 nearZ, f32 farZ, const f32* scale, const f32* offset) {
    (void)ctx;(void)nearZ;(void)farZ;
    hardwareViewport=(Ps3glRasterRect){x,y,w,h};
    if((unsigned)y+h>hardwareTarget.height || (unsigned)x+w>hardwareTarget.width)
        fprintf(stderr,"INVALID RSX VIEWPORT: (%u,%u %ux%u) target=%ux%u\n",x,y,w,h,hardwareTarget.width,hardwareTarget.height);
    assertBounds(hardwareViewport);
    memcpy(hardwareScale,scale,sizeof(hardwareScale));memcpy(hardwareOffset,offset,sizeof(hardwareOffset));
    ++viewportCalls;
}
void rsxSetScissor(gcmContextData* ctx, u16 x, u16 y, u16 w, u16 h) {
    (void)ctx;hardwareScissor=(Ps3glRasterRect){x,y,w,h};assertBounds(hardwareScissor);
}
void rsxClearSurface(gcmContextData* ctx, u32 mask) {(void)ctx;(void)mask;assertBounds(hardwareScissor);++clears;}
void rsxLoadTexture(gcmContextData* ctx, u8 slot, const gcmTexture* texture) {
    (void)ctx;(void)slot;assert(texture->width && texture->height);++textureCalls;
}
void rsxDrawVertexBegin(gcmContextData* ctx, u32 mode) {
    (void)ctx;(void)mode;assertBounds(hardwareViewport);assertBounds(hardwareScissor);++draws;
    if(pickerRecording){
        assert(modelUploaded&&projectionUploaded);
        assert(hardwareModel[0]==1&&hardwareModel[5]==1&&hardwareModel[15]==1);
        assert(fabsf(hardwareProjection[0]-2.0f/640)<1e-7f&&fabsf(hardwareProjection[5]+2.0f/480)<1e-7f);
        assert(hardwareProjection[12]==-1&&hardwareProjection[13]==1&&hardwareProjection[15]==1);
        assert(hardwareColorMask==(GCM_COLOR_MASK_R|GCM_COLOR_MASK_G|GCM_COLOR_MASK_B|GCM_COLOR_MASK_A));
        if(hardwareTextureEnabled)++pickerTextured;else ++pickerSolid;
    }
}

static GLuint surface(GLuint* texture, int w, int h) {
    GLuint fbo;glGenTextures(1,texture);glBindTexture(GL_TEXTURE_2D,*texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*texture,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    return fbo;
}
static void quad(void) {
    glBegin(GL_QUADS);
    glVertex2f(0,0);glVertex2f(1,0);glVertex2f(1,1);glVertex2f(0,1);glEnd();
}

int main(void) {
    testContext.current=commands;
    _opengl_state.depth_far=1;
    static rsxProgramConst uniforms[PS3GL_Uniform_Count],uniform;
    for(unsigned i=0;i<PS3GL_Uniform_Count;++i){uniforms[i].index=i;_opengl_state.prog_consts[i]=&uniforms[i];}
    _opengl_dummy_texture.width=_opengl_dummy_texture.height=1;
    static rsxProgramAttrib sampler={.index=0};_opengl_state.ffp_tex_unit=&sampler;
    glColorMask(1,1,1,1);glClearDepth(1);
    GLuint appTexture;GLuint app=surface(&appTexture,640,480);

    for(unsigned cycle=0;cycle<100;++cycle) {
        // Last frame's GUI uses a physical 720p screen viewport.
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(160,0,960,720);quad();
        // The next Step event builds obj_growtangle's 75x50 custom box.
        GLuint tempTexture;GLuint temp=surface(&tempTexture,75,50);
        glViewport(0,0,75,50);glDisable(GL_SCISSOR_TEST);glClear(GL_COLOR_BUFFER_BIT);quad();
        unsigned char pixels[75*50*4];glReadPixels(0,0,75,50,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLuint captured;glGenTextures(1,&captured);glBindTexture(GL_TEXTURE_2D,captured);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,75,50,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        // surface_reset_target returns to the app surface while its caller
        // still has the last GUI port rectangle. Do not submit that as u16.
        glBindFramebuffer(GL_FRAMEBUFFER,app);glViewport(160,0,960,720);
        glDeleteTextures(1,&tempTexture);glDeleteFramebuffers(1,&temp);
        unsigned beforeViewport=viewportCalls,beforeTexture=textureCalls;
        glClear(GL_COLOR_BUFFER_BIT);
        assert(viewportCalls==beforeViewport && textureCalls==beforeTexture);
        assert(hardwareTarget.width==640 && hardwareScissor.w==640 && hardwareScissor.h==480);
        quad();
        assert(hardwareViewport.x==160 && hardwareViewport.y==0);
        assert(hardwareViewport.w==480 && hardwareViewport.h==480);
        // Clipping the raster bounds must preserve the original transform.
        assert(hardwareScale[0]==480 && hardwareScale[1]==-360);
        assert(hardwareOffset[0]==640 && hardwareOffset[1]==120);
        glViewport(0,0,640,480);quad();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1280,720);glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D,captured);quad();glDeleteTextures(1,&captured);
    }
    // Signed scissor clipping and target-height conversion happen at draw
    // time, even if the FBO changes after glScissor/glViewport are called.
    glViewport(0,0,640,480);glScissor(-10,-5,30,20);glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,app);quad();
    assert(hardwareScissor.x==0 && hardwareScissor.y==465 && hardwareScissor.w==20 && hardwareScissor.h==15);
    glViewport(INT_MIN,INT_MAX,INT_MAX,INT_MAX);quad();assert(hardwareViewport.w==0);
    glScissor(INT_MAX,INT_MIN,INT_MAX,INT_MAX);quad();assert(hardwareScissor.w==0);
    glViewport(0,0,-1,1);assert(glGetError()==GL_INVALID_VALUE);
    glScissor(0,0,1,-1);assert(glGetError()==GL_INVALID_VALUE);
    glDisable(GL_SCISSOR_TEST);glViewport(0,0,640,480);quad();
    unsigned cachedViewport=viewportCalls,cachedTextures=textureCalls;
    unsigned cachedVp=vertexProgramLoads,cachedFp=fragmentProgramLoads;
    for(unsigned i=0;i<1000;++i)quad();
    assert(viewportCalls==cachedViewport+1000&&textureCalls==cachedTextures+1000);
    assert(vertexProgramLoads==cachedVp+1000&&fragmentProgramLoads==cachedFp+1000);
    glDepthMask(true);glColorMask(1,1,1,1);quad();
    u32 fullMask=hardwareColorMask;
    glDepthMask(false);glColorMask(0,0,0,0);glScissor(2,3,4,5);glEnable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    assert(hardwareColorMask==0&&hardwareDepthWrite==0);
    glDepthMask(true);glColorMask(1,1,1,1);glDisable(GL_SCISSOR_TEST);quad();
    assert(hardwareColorMask==fullMask&&hardwareDepthWrite==1&&hardwareScissor.w==640&&hardwareScissor.h==480);
    // Every draw resubmits program state; uniforms are uploaded on each API call.
    static rsxFragmentProgram customProgram;static u32 customCode;
    _opengl_state.shaders[1]=(struct ps3gl_shader){.allocated=true,.type=GL_FRAGMENT_SHADER,.blob=&customProgram,.fpUcode=&customCode,.fpOffset=64};
    _opengl_state.programs[1]=(struct ps3gl_program){.allocated=true,.linked=true,.fragmentShader=1,.uniformCount=1};
    _opengl_state.programs[1].uniforms[0].stage=PS3GL_LOC_STAGE_FP;
    _opengl_state.programs[1].uniforms[0].constHandle=&uniform;
    glUseProgram(1);GLint location=PS3GL_LOC_PACK(PS3GL_LOC_STAGE_FP,0);
    unsigned writes=scalarUniformWrites;
    glUniform1f(location,.5f);glUniform1f(location,.5f);assert(scalarUniformWrites==writes+2);
    quad();cachedFp=fragmentProgramLoads;
    quad();assert(fragmentProgramLoads==cachedFp+1);
    glUniform1f(location,.75f);quad();assert(scalarUniformWrites==writes+3&&fragmentProgramLoads==cachedFp+2);
    glUseProgram(0);
    memset(&_opengl_state.shaders[1],0,sizeof(_opengl_state.shaders[1]));
    memset(&_opengl_state.programs[1],0,sizeof(_opengl_state.programs[1]));
    // Actual glReadPixels cropping, padded source rows, RGBA order, row flip
    // and >1MiB chunk boundaries. The mock transfer completes before returning.
    GLuint readTexture;GLuint readFbo=surface(&readTexture,511,800);
    struct ps3gl_texture* read=&_opengl_state.textures[readTexture];
    for(unsigned y=0;y<800;++y)for(unsigned x=0;x<511;++x) {
        unsigned char* p=read->data+y*read->gcmTexture.pitch+x*4;
        p[0]=255;p[1]=x%256;p[2]=y%256;p[3]=(x+y)%256;
    }
    unsigned char* output=malloc(513*802*4);unsigned beforeTransfers=readbackCalls;
    glReadPixels(-1,-1,513,802,GL_RGBA,GL_UNSIGNED_BYTE,output);
    assert(readbackCalls==beforeTransfers+2);
    for(unsigned y=0;y<802;++y)for(unsigned x=0;x<513;++x) {
        unsigned char* p=output+(y*513+x)*4;
        if(x==0||x==512||y==0||y==801)assert(p[0]==0&&p[1]==0&&p[2]==0&&p[3]==0);
        else {unsigned sx=x-1,sy=800-y;assert(p[0]==sx%256&&p[1]==sy%256&&p[2]==(sx+sy)%256&&p[3]==255);}
    }
    free(output);glDeleteFramebuffers(1,&readFbo);glDeleteTextures(1,&readTexture);
    // Run the real picker through production PS3GL, not desktop OpenGL.
    // Verify its matrices, target, complete state and textured/solid paths at
    // 480p, 720p and the 1080p mode from the user's black-screen log.
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    PickerUI ui;assert(PickerUI_init(&ui));ui.alpha=1;ui.slide=0;
    PickerView view={0};view.chapters=true;view.footer=-1;view.status="Searching...";
    for(unsigned i=0;i<5;++i)view.enabled[i]=true;
    const unsigned sizes[3][2]={{640,480},{1280,720},{1920,1080}};
    pickerRecording=true;
    for(unsigned i=0;i<3;++i){
        display_width=sizes[i][0];display_height=sizes[i][1];
        glColorMask(0,0,0,0);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_NEVER,1);
        unsigned texturedBefore=pickerTextured,solidBefore=pickerSolid;
        for(unsigned frame=0;frame<2;++frame){PickerUI_draw(&ui,&view,display_width,display_height);ps3glSwapBuffers();}
        assert(hardwareTarget.width==display_width&&hardwareTarget.height==display_height);
        assert(pickerTextured>texturedBefore+100&&pickerSolid>=solidBefore+14);
        assert(!_opengl_state.alpha_test_enabled&&_opengl_state.active_program==0&&_opengl_state.active_texture_unit==0);
    }
    pickerRecording=false;PickerUI_deinit(&ui);
    glDeleteFramebuffers(1,&app);glDeleteTextures(1,&appTexture);
    assert(liveAllocations==0 && glGetError()==GL_NO_ERROR);
    printf("PASS: %u clears, %u draws; 100 capture/free cycles; complete state across 1000 draws; chunked DMA/cropped RGBA readback\n",clears,draws);
    puts("PASS: real picker through PS3GL at 480p/720p/1080p; matrices, display target, masks, textured glyphs and untextured separators");
    return 0;
}
