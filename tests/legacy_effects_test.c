#include "../src/gl_legacy/gl_legacy_renderer.c"
#include <GLFW/glfw3.h>
#include <assert.h>

// This fixture installs GPU textures itself. Asset loading must stay unused.
void DataWin_loadTxtrIfNeeded(DataWin* dw,uint32_t id){(void)dw;(void)id;assert(!"unexpected asset load");}
bool DataWin_isVersionAtLeast(const DataWin* dw,uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)dw;(void)a;(void)b;(void)c;(void)d;return true;}
uint8_t* ImageDecoder_decodeToRgba(const uint8_t* b,size_t n,bool modern,int* w,int* h){(void)b;(void)n;(void)modern;(void)w;(void)h;assert(!"unexpected image decode");return NULL;}

static void drawQuad(void) {
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
}

int main(void) {
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* window = glfwCreateWindow(16, 16, "effects", NULL, NULL);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc) glfwGetProcAddress));
    GLLegacyRenderer renderer = {0};
    Renderer* base = &renderer.base;
    glGpuSetBlendMode(base, bm_normal);
    BlendFactors saved = glGpuGetBlendFactors(base);
    assert(renderer.currentSFactor == bm_src_alpha && renderer.currentDFactor == bm_inv_src_alpha);
    glGpuSetBlendMode(base, bm_min);
    glGpuSetBlendModeExt(base, bm_src_alpha, bm_inv_src_alpha, bm_dest_alpha, bm_zero);
    GLint equation, srcAlpha, dstAlpha;
    glGetIntegerv(GL_BLEND_EQUATION, &equation);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    assert(equation == GL_FUNC_ADD && srcAlpha == GL_DST_ALPHA && dstAlpha == GL_ZERO);
    glGpuSetBlendModeExt(base, bm_src_alpha, bm_inv_src_alpha, bm_src_alpha, bm_inv_src_alpha);
    BlendFactors restored = glGpuGetBlendFactors(base);
    assert(memcmp(&saved, &restored, sizeof(saved)) == 0);

    GLuint fbo, target, sprite;
    glGenTextures(1, &target); glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 4, 4);
    glGenTextures(1, &sprite); glBindTexture(GL_TEXTURE_2D, sprite);
    unsigned char texel[] = {200, 100, 50, 64};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glColor4f(1, 1, 1, 1);
    glGpuSetFog(base, true, 0x1E140A); // BGR -> RGB (10,20,30)
    drawQuad();
    unsigned char pixel[4];
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30 && pixel[3] == 64);
    glGpuSetFog(base, false, 0);
    drawQuad(); glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(memcmp(pixel, texel, 4) == 0);

    // Pixel reads must use GML's top-left origin, preserve alpha, and leave
    // independent read/draw framebuffer bindings and pack alignment intact.
    int32_t width=4, height=4;
    renderer.surfaces=&fbo; renderer.surfaceWidth=&width; renderer.surfaceHeight=&height; renderer.surfaceCount=1;
    glEnable(GL_SCISSOR_TEST); glScissor(0,2,4,2);
    glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,8);
    assert(glLegacySurfaceGetPixel(base,0,0,0,pixel));
    assert(pixel[0]==255 && pixel[1]==0 && pixel[2]==0 && pixel[3]==255);
    assert(glLegacySurfaceGetPixel(base,0,0,3,pixel));
    assert(memcmp(pixel,texel,4)==0);
    GLint binding, pack;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&binding); assert(binding==0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&binding); assert(binding==(GLint)fbo);
    glGetIntegerv(GL_PACK_ALIGNMENT,&pack); assert(pack==8);
    assert(!glLegacySurfaceGetPixel(base,-1,0,0,pixel));
    assert(!glLegacySurfaceGetPixel(base,0,-1,0,pixel));
    assert(!glLegacySurfaceGetPixel(base,0,4,0,pixel));
    assert(!glLegacySurfaceGetPixel(base,0,0,4,pixel));
    assert(glGetError() == GL_NO_ERROR);
    // Filled UI rectangles cover their inclusive last pixel exactly once.
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,4,4,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    renderer.whiteTexture=sprite;glDisable(GL_TEXTURE_2D);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glDrawRectangle(base,0,0,1,1,0xFFFFFF,1,false);
    unsigned char pixels[4*4*4];glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)
        assert(pixels[(y*4+x)*4+3]==((x<2 && y>=2)?255:0));
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &sprite); glDeleteTextures(1, &target);
    glfwDestroyWindow(window); glfwTerminate();
    puts("PASS: fog/alpha, blend round-trip, pixel readback orientation/bounds/state");
    return 0;
}
