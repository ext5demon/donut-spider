#include "ps3_textures.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* GPU upload is outside the bundle parser test; all file parsing is production code. */
static bool uploadWorks = true;
void PS3SessionLog_event(const char* format, ...) { (void)format; }
void glGenTextures(GLsizei n, GLuint* ids) { while (n--) *ids++ = 1; }
void glDeleteTextures(GLsizei n, const GLuint* ids) { (void)n; (void)ids; }
void glActiveTexture(GLenum unit) { (void)unit; }
void glBindTexture(GLenum target, GLuint id) { (void)target; (void)id; }
void glTexParameteri(GLenum target, GLenum name, GLint value) { (void)target; (void)name; (void)value; }
void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                 GLint border, GLenum format, GLenum type, const GLvoid* data) {
    (void)target; (void)level; (void)internal; (void)w; (void)h;
    (void)border; (void)format; (void)type; (void)data;
}
bool ps3glTextureReady(GLuint id) { return id != 0 && uploadWorks; }

static void writeBundle(const char* path, unsigned bytes, unsigned offset, unsigned palette, bool truncate) {
    FILE* f = fopen(path,"wb"); assert(f);
    const uint8_t header[] = {0,0,1,0,1,0,1};
    uint8_t clut[1024] = {0};
    for(unsigned i=0;i<256;++i){clut[i*4]=i;clut[i*4+1]=255-i;clut[i*4+2]=i^0xaa;clut[i*4+3]=255;}
    uint8_t map[] = {0,(uint8_t)palette};
    uint8_t page[] = {0,3,0,2,0,0,0,(uint8_t)offset,0,0,0,(uint8_t)bytes};
    const uint8_t pixels[] = {1,2,3,4,5,6};
    assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
    fwrite(clut,1,sizeof(clut),f); fwrite(map,1,2,f); fwrite(page,1,12,f);
    fwrite(pixels,1,truncate ? 5 : 6,f); fclose(f);
}
int main(void) {
    char path[] = "/tmp/spider-textures-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    writeBundle(path,6,0,0,false);
    assert(PS3Textures_init(path));
    int w=0,h=0; uint8_t* pixels=NULL;
    assert(PS3Textures_loadPage(0,&w,&h,&pixels));
    assert(w==3 && h==2 && pixels[0]==1 && pixels[5]==6); free(pixels);
    uint8_t colors[1024];assert(PS3Textures_readClut(0,colors));
    assert(colors[4]==1&&colors[5]==254&&colors[6]==171&&colors[7]==255);
    assert(!PS3Textures_readClut(-1,colors)&&!PS3Textures_readClut(1,colors));
    TexturePageItem small={.texturePageId=0,.sourceWidth=3,.sourceHeight=2};
    assert(PS3Textures_readTpagRGBA(&small,0,&pixels));
    for(unsigned i=0;i<6;++i)assert(pixels[i*4]==i+1&&pixels[i*4+1]==254-i&&pixels[i*4+3]==255);
    free(pixels);small.sourceX=1;small.sourceY=1;small.sourceWidth=2;small.sourceHeight=1;
    assert(PS3Textures_readTpagRGBA(&small,0,&pixels)&&pixels[0]==5&&pixels[4]==6);free(pixels);
    small.sourceX=2;assert(!PS3Textures_readTpagRGBA(&small,0,&pixels)&&pixels==NULL);
    assert(!PS3Textures_loadPage(1,&w,&h,&pixels) && pixels==NULL);
    assert(!PS3Textures_loadPage(0,NULL,&h,&pixels));
    DataWin dw = {0}; TexturePageItem item = {0};
    dw.tpag.count=1; dw.tpag.items=&item; item.present=true; item.texturePageId=0;
    item.sourceWidth=3; item.sourceHeight=2;
    assert(PS3Textures_validate(&dw));
    item.sourceX=1; assert(!PS3Textures_validate(&dw)); item.sourceX=0;
    dw.tpag.count=2; assert(!PS3Textures_validate(&dw));
    PS3Textures_free();
    writeBundle(path,5,0,0,false); assert(!PS3Textures_init(path));
    writeBundle(path,6,1,0,false); assert(!PS3Textures_init(path));
    writeBundle(path,6,0,1,false); assert(!PS3Textures_init(path));
    writeBundle(path,6,0,0,true); assert(!PS3Textures_init(path));
    writeBundle(path,6,0,0,false); uploadWorks=false; assert(!PS3Textures_init(path));
    uploadWorks=true; assert(PS3Textures_init(path)); PS3Textures_free();
    unlink(path);
    puts("PASS: streamed bundle bytes, truncation, invalid palettes/offsets, WAD mismatch, upload failure and recovery");
    return 0;
}
