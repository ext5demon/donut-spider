// The legacy renderer still owns geometry/transforms. On desktop the original
// WAD fragment program runs with fixed-function vertex inputs. PS3 uses its
// indexed texture CLUT for palette substitution, retaining exact WAD colors.
static int legacyShaderKind(GLLegacyRenderer* gl,int32_t index) {
    if(!gl->base.dataWin||index<0||(uint32_t)index>=gl->base.dataWin->shdr.count)return 0;
    Shader* shader=&gl->base.dataWin->shdr.shaders[index];
    if(!shader->present||!shader->name)return 0;
    if(!strcmp(shader->name,"shd_pal_swapper"))return 1;
    if(!strcmp(shader->name,"shd_hue"))return 2;
    if(!strcmp(shader->name,"shd_grayscale"))return 3;
    return 0;
}

#ifndef PLATFORM_PS3
static bool legacyCompileShader(GLLegacyRenderer* gl,int32_t index) {
    LegacyShader* state=&gl->shaders[index];
    if(state->attempted)return state->compiled;
    state->attempted=true;
    const char* vertex="#version 120\nvarying vec2 v_vTexcoord;varying vec4 v_vColour;varying vec2 v_texcoord;void main(){gl_Position=ftransform();v_vTexcoord=gl_MultiTexCoord0.xy;v_texcoord=v_vTexcoord;v_vColour=gl_Color;}";
    const char* fragment=gl->base.dataWin->shdr.shaders[index].glsl_Fragment;
    if(!fragment||!glCreateShader)return false;
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,NULL);glCompileShader(vs);
    glShaderSource(fs,1,&fragment,NULL);glCompileShader(fs);
    GLint ok1=0,ok2=0;glGetShaderiv(vs,GL_COMPILE_STATUS,&ok1);glGetShaderiv(fs,GL_COMPILE_STATUS,&ok2);
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!ok1||!ok2||!linked) {
        char log[2048];glGetShaderInfoLog(fs,sizeof(log),NULL,log);fprintf(stderr,"GL shader %s failed: %s\n",gl->base.dataWin->shdr.shaders[index].name,log);
        glDeleteProgram(program);program=0;
    }
    glDeleteShader(vs);glDeleteShader(fs);
    state->program=program;state->compiled=program!=0;return state->compiled;
}
#endif

static bool glShaderIsCompiled(Renderer* renderer,int32_t index) {
    GLLegacyRenderer* gl=(GLLegacyRenderer*)renderer;
    if(!legacyShaderKind(gl,index))return false;
#ifdef PLATFORM_PS3
    return true;
#else
    return legacyCompileShader(gl,index);
#endif
}
static bool glShadersSupported(void){return true;}
static void glGpuSetShader(Renderer* renderer,int32_t index) {
    GLLegacyRenderer* gl=(GLLegacyRenderer*)renderer;
    gl->shaderIndex=glShaderIsCompiled(renderer,index)?index:-1;
    renderer->currentShader=gl->shaderIndex;
}
static void glGpuResetShader(Renderer* renderer) {
    GLLegacyRenderer* gl=(GLLegacyRenderer*)renderer;gl->shaderIndex=-1;renderer->currentShader=-1;glUseProgram(0);
}
static int32_t glShaderGetUniform(Renderer* renderer,int32_t index,char* name) {
    if(!name||!glShaderIsCompiled(renderer,index))return -1;
    const char* names[]={"u_Uvs","u_paletteId","u_pixelSize","u_Position"};
    int kind=legacyShaderKind((GLLegacyRenderer*)renderer,index);
    for(int i=0;i<4;++i)if(!strcmp(name,names[i])&&((kind==1&&i<3)||(kind==2&&i==3)))return index*4+i;
    return -1;
}
static int32_t glShaderGetSamplerIndex(Renderer* renderer,int32_t index,char* name) {
    return name&&glShaderIsCompiled(renderer,index)&&legacyShaderKind((GLLegacyRenderer*)renderer,index)==1&&!strcmp(name,"u_palTexture")?1:-1;
}
static void glShaderSetUniformF(Renderer* renderer,int32_t handle,int32_t count,float a,float b,float c,float d) {
    GLLegacyRenderer* gl=(GLLegacyRenderer*)renderer;
    if(handle<0||(uint32_t)(handle/4)>=renderer->dataWin->shdr.count)return;
    LegacyShader* state=&gl->shaders[handle/4];
    if(handle%4==0&&count==4){state->uvs[0]=a;state->uvs[1]=b;state->uvs[2]=c;state->uvs[3]=d;}
    else if(handle%4==1&&count==1)state->paletteId=a;
    else if(handle%4==2&&count==2){state->pixelSize[0]=a;state->pixelSize[1]=b;}
    else if(handle%4==3&&count==1)state->hue=a;
}
static void glShaderSetUniformFArray(Renderer* renderer,int32_t handle,float* values,uint32_t count) {
    if(values&&count&&count<=4)glShaderSetUniformF(renderer,handle,count,values[0],count>1?values[1]:0,count>2?values[2]:0,count>3?values[3]:0);
}
static void glShaderSetUniformI(Renderer* renderer,int32_t handle,int32_t count,int32_t a,int32_t b,int32_t c,int32_t d) {
    glShaderSetUniformF(renderer,handle,count,(float)a,(float)b,(float)c,(float)d);
}

#ifdef PLATFORM_PS3
static GLuint legacyMappedPalette(GLLegacyRenderer* gl,int32_t sourceTpag,LegacyShader* state) {
    int32_t pal=(int32_t)gl->paletteTextureHandle-1;
    if(pal<0||(uint32_t)pal>=gl->base.dataWin->tpag.count)return 0;
    // Different animation frames/body parts commonly share one source CLUT.
    float sourcePaletteV=PS3Textures_getTpagPaletteV(sourceTpag);
    for(unsigned i=0;i<32;++i) {
        LegacyPaletteCache* cache=&gl->paletteCache[i];
        if(cache->texture&&cache->sourcePaletteV==sourcePaletteV&&cache->paletteTpag==pal&&
           cache->paletteId==state->paletteId&&!memcmp(cache->uvs,state->uvs,sizeof(cache->uvs))) {
            cache->used=++gl->paletteClock;return cache->texture;
        }
    }
    TexturePageItem* item=&gl->base.dataWin->tpag.items[pal];
    if(gl->palettePixelsTpag!=pal) {
        free(gl->palettePixels);gl->palettePixels=NULL;gl->palettePixelsTpag=-1;
        if(!PS3Textures_readTpagRGBA(item,pal,&gl->palettePixels))return 0;
        gl->palettePixelsTpag=pal;
    }
    int pageW=gl->textureWidths[item->texturePageId],pageH=gl->textureHeights[item->texturePageId];
    int firstX=(int)floorf(state->uvs[0]*pageW)-item->sourceX;
    int firstY=(int)floorf(state->uvs[1]*pageH)-item->sourceY;
    int endY=(int)lroundf(state->uvs[3]*pageH)-item->sourceY;
    if(firstX<0||firstY<0||endY<0)return 0;
    uint8_t source[1024],mapped[1024];if(!PS3Textures_readClut(sourceTpag,source))return 0;
    paletteSwapRow(mapped,source,gl->palettePixels,item->sourceWidth,item->sourceHeight,firstX,firstY,endY,state->paletteId);
    unsigned slot=0;for(unsigned i=1;i<32;++i)if(gl->paletteCache[i].used<gl->paletteCache[slot].used)slot=i;
    LegacyPaletteCache* cache=&gl->paletteCache[slot];
    glActiveTexture(GL_TEXTURE1);
    if(!cache->texture)glGenTextures(1,&cache->texture);
    glBindTexture(GL_TEXTURE_2D,cache->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,mapped);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    cache->sourcePaletteV=sourcePaletteV;cache->paletteTpag=pal;cache->paletteId=state->paletteId;
    memcpy(cache->uvs,state->uvs,sizeof(cache->uvs));cache->used=++gl->paletteClock;
    return cache->texture;
}
#endif

static void glLegacyShaderBegin(GLLegacyRenderer* gl,int32_t tpagIndex) {
    int kind=legacyShaderKind(gl,gl->shaderIndex);
    LegacyShader* state=kind?&gl->shaders[gl->shaderIndex]:NULL;
#ifdef PLATFORM_PS3
    float paletteV=PS3Textures_getTpagPaletteV(tpagIndex);
    bool indexed=paletteV>=0;
    GLuint palette=indexed?PS3Textures_getClutTexture():0;
    if(indexed&&kind==1){GLuint mapped=legacyMappedPalette(gl,tpagIndex,state);if(mapped){palette=mapped;paletteV=.5f;}}
    if(!indexed&&kind!=2&&kind!=3)return;
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,palette);glEnable(GL_TEXTURE_2D);glActiveTexture(GL_TEXTURE0);
    GLuint program=gPalettedProgram;
    if(kind==2||kind==3) {
        if(!gl->colorEffectProgram) {
            extern unsigned char color_effect_fpo[];extern unsigned int color_effect_fpo_len;
            GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);glShaderBinary(1,&shader,PS3GL_SHADER_BINARY_FPO,color_effect_fpo,color_effect_fpo_len);
            gl->colorEffectShader=shader;
            gl->colorEffectProgram=glCreateProgram();glAttachShader(gl->colorEffectProgram,shader);glLinkProgram(gl->colorEffectProgram);
            glUseProgram(gl->colorEffectProgram);
            glUniform1i(glGetUniformLocation(gl->colorEffectProgram,"uIndex"),0);
            glUniform1i(glGetUniformLocation(gl->colorEffectProgram,"uPalette"),1);
        }
        program=gl->colorEffectProgram;
    }
    glUseProgram(program);
    glUniform1f(program==gPalettedProgram?gPalettedUPaletteVLoc:glGetUniformLocation(program,"uPaletteV"),paletteV);
    glUniform4f(program==gPalettedProgram?gPalettedFogLoc:glGetUniformLocation(program,"uFogColor"),BGR_R(gl->fogColor)/255.f,BGR_G(gl->fogColor)/255.f,BGR_B(gl->fogColor)/255.f,gl->fogEnable?1.f:0.f);
    if(kind==2||kind==3)glUniform4f(glGetUniformLocation(program,"uEffect"),indexed?1.f:0.f,kind==2?1.f:2.f,state->hue,0);
#else
    (void)tpagIndex;
    if(!state||!legacyCompileShader(gl,gl->shaderIndex))return;
    glUseProgram(state->program);
    glUniform1i(glGetUniformLocation(state->program,"gm_BaseTexture"),0);
    if(kind==1) {
        GLint baseTexture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&baseTexture);
        TexturePageItem* item=NULL;GLuint texture=0;int32_t w=0,h=0;
        if(glLegacyResolveTextureHandle(gl,gl->paletteTextureHandle,&item,&texture,&w,&h)) {
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0);
            glUniform1i(glGetUniformLocation(state->program,"u_palTexture"),1);
        }
        glBindTexture(GL_TEXTURE_2D,(GLuint)baseTexture);
        glUniform4fv(glGetUniformLocation(state->program,"u_Uvs"),1,state->uvs);
        glUniform2fv(glGetUniformLocation(state->program,"u_pixelSize"),1,state->pixelSize);
        glUniform1f(glGetUniformLocation(state->program,"u_paletteId"),state->paletteId);
    } else if(kind==2)glUniform1f(glGetUniformLocation(state->program,"u_Position"),state->hue);
#endif
}
static void glLegacyShaderEnd(void) {
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);glDisable(GL_TEXTURE_2D);glActiveTexture(GL_TEXTURE0);
}
