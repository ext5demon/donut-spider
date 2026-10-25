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
    // GML can enable alpha testing without ever setting its reference.
    // Start from hostile host GL state and exercise the real renderer init.
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_ALWAYS, 1.0f);
    DataWin initData = {.txtr = {.count = 1}, .shdr = {.count = 1}};
    GLLegacyRenderer* initialized = safeCalloc(1, sizeof(*initialized));
    glInit(&initialized->base, &initData);
    GLint alphaFunc; GLfloat alphaRef;
    glGetIntegerv(GL_ALPHA_TEST_FUNC, &alphaFunc);
    glGetFloatv(GL_ALPHA_TEST_REF, &alphaRef);
    assert(!glIsEnabled(GL_ALPHA_TEST) && alphaFunc == GL_GREATER && alphaRef == 0.0f);
    glDestroy(&initialized->base);
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

    // Knight-style destination-alpha masking: transparent areas of a layer
    // must not erase the mask. Enabling the test uses the startup reference.
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glGpuSetBlendEnable(base, true);
    glGpuSetBlendModeExt(base, bm_src_alpha, bm_one, bm_dest_alpha, bm_zero);
    glGpuSetAlphaTestEnable(base, true);
    unsigned char transparent[] = {200, 100, 50, 0};
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, transparent);
    drawQuad(); glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    drawQuad(); glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(abs((int)pixel[0] - 50) <= 1 && abs((int)pixel[3] - 64) <= 1);
    glGpuSetAlphaTestEnable(base, false); glGpuSetBlendEnable(base, false);
    glGpuSetBlendMode(base, bm_normal);
    drawQuad();

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
    // Tenna's rings are a brightness-derived shadow mask, not white circles.
    // Exercise the legacy backend's real shader selection and draw activation.
    Shader shaderDefs[3]={
        {.present=true,.name="shd_luminosity_to_transparency",.glsl_Fragment="#version 120\nuniform sampler2D gm_BaseTexture;varying vec2 v_vTexcoord;varying vec4 v_vColour;void main(){vec4 s=texture2D(gm_BaseTexture,v_vTexcoord);gl_FragColor=vec4(0,0,0,(1-dot(s.rgb,vec3(1.0/3.0)))*s.a*v_vColour.a);}"},
        {.present=true,.name="shd_grayscalesand",.glsl_Fragment="#version 120\nuniform sampler2D gm_BaseTexture;uniform vec3 sand1,sand2,sand3,sand4,sandcol;varying vec2 v_vTexcoord;void main(){vec4 s=texture2D(gm_BaseTexture,v_vTexcoord);vec3 c=vec3(dot(s.rgb,vec3(.3,.59,.11)));vec3 b=floor(s.rgb*255.0+.001);if(distance(b,sand1)<.004||distance(b,sand2)<.004||distance(b,sand3)<.004||distance(b,sand4)<.004)c=sandcol;gl_FragColor=vec4(c,s.a);}"},
        {.present=true,.name="shd_tone",.glsl_Fragment="#version 120\nuniform sampler2D gm_BaseTexture;uniform float saturation,brightness,tone_factor;uniform vec3 tone_dark,tone_light;varying vec2 v_vTexcoord;void main(){vec4 s=texture2D(gm_BaseTexture,v_vTexcoord);float l=dot(s.rgb,vec3(.3,.59,.11));gl_FragColor=vec4(mix(vec3(l),s.rgb,saturation)*mix(vec3(1),mix(tone_dark,tone_light,l),tone_factor)*brightness,s.a);}"}
    };
    DataWin data={.shdr={.count=3,.shaders=shaderDefs}};LegacyShader shaderStates[3]={0};
    renderer.base.dataWin=&data;renderer.shaders=shaderStates;renderer.shaderIndex=-1;
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_TEXTURE_2D);glDisable(GL_BLEND);glBindTexture(GL_TEXTURE_2D,sprite);glColor4f(1,1,1,.5f);
    glGpuSetShader(base,0);assert(base->currentShader==0);glLegacyShaderBegin(&renderer,-1);drawQuad();glLegacyShaderEnd();
    glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0&&abs((int)pixel[3]-17)<=1);
    glGpuSetShader(base,1);
    int sand=glShaderGetUniform(base,1,"sand1"),gray=glShaderGetUniform(base,1,"sandcol");assert(sand>=0&&gray>=0);
    glShaderSetUniformF(base,sand,3,200,100,50,0);glShaderSetUniformF(base,gray,3,.82f,.82f,.82f,0);
    glLegacyShaderBegin(&renderer,-1);drawQuad();glLegacyShaderEnd();glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(abs((int)pixel[0]-209)<=1&&pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&pixel[3]==64);
    glGpuSetShader(base,2);
    glShaderSetUniformF(base,glShaderGetUniform(base,2,"saturation"),1,0,0,0,0);
    glShaderSetUniformF(base,glShaderGetUniform(base,2,"brightness"),1,.5f,0,0,0);
    glLegacyShaderBegin(&renderer,-1);drawQuad();glLegacyShaderEnd();glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(abs((int)pixel[0]-62)<=1&&pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&pixel[3]==64);
    glGpuResetShader(base);assert(base->currentShader==-1&&glGetError()==GL_NO_ERROR);
    for(int i=0;i<3;++i)glDeleteProgram(shaderStates[i].program);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &sprite); glDeleteTextures(1, &target);
    glfwDestroyWindow(window); glfwTerminate();
    puts("PASS: fog/alpha, blend round-trip, pixel readback orientation/bounds/state");
    return 0;
}
