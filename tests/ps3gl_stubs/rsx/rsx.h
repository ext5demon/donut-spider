#ifndef TEST_RSX_H
#define TEST_RSX_H
/* Host-only RSX API recorder. No GPU, shader execution or FIFO emulation.
 * Constants use the PSL1GHT API values; layouts below expose only API fields. */
#include <ppu-types.h>
#include <stddef.h>
#include <stdlib.h>
#define GCM_CLEAR_A 0x80
#define GCM_CLEAR_B 0x40
#define GCM_CLEAR_G 0x20
#define GCM_CLEAR_M 0xf3
#define GCM_CLEAR_R 0x10
#define GCM_CLEAR_S 0x02
#define GCM_CLEAR_Z 0x01
#define GCM_COLOR_MASK_A 0x01000000
#define GCM_COLOR_MASK_B 0x00000001
#define GCM_COLOR_MASK_G 0x00000100
#define GCM_COLOR_MASK_R 0x00010000
#define GCM_FALSE 0
#define GCM_FOG_MODE_LINEAR 0x2601
#define GCM_INVALIDATE_TEXTURE 1
#define GCM_LOCATION_RSX 0
#define GCM_MAX_MRT_COUNT 4
#define GCM_SHADE_MODEL_FLAT 0x1D00
#define GCM_SHADE_MODEL_SMOOTH 0x1D01
#define GCM_SURFACE_A8R8G8B8 8
#define GCM_SURFACE_CENTER_1 0
#define GCM_SURFACE_TARGET_0 1
#define GCM_SURFACE_TYPE_LINEAR 1
#define GCM_SURFACE_ZETA_Z24S8 2
#define GCM_TEXTURE_BORDER 4
#define GCM_TEXTURE_CLAMP_TO_EDGE 3
#define GCM_TEXTURE_CONVOLUTION_QUINCUNX 1
#define GCM_TEXTURE_DIMS_2D 2
#define GCM_TEXTURE_FORMAT_A8R8G8B8 5
#define GCM_TEXTURE_FORMAT_B8 1
#define GCM_TEXTURE_FORMAT_LIN 0x20
#define GCM_TEXTURE_LINEAR 2
#define GCM_TEXTURE_LINEAR_MIPMAP_LINEAR 6
#define GCM_TEXTURE_LINEAR_MIPMAP_NEAREST 4
#define GCM_TEXTURE_MAX_ANISO_1 0
#define GCM_TEXTURE_MIRRORED_REPEAT 2
#define GCM_TEXTURE_MIRROR_ONCE_CLAMP_TO_EDGE 6
#define GCM_TEXTURE_NEAREST 1
#define GCM_TEXTURE_NEAREST_MIPMAP_LINEAR 5
#define GCM_TEXTURE_NEAREST_MIPMAP_NEAREST 3
#define GCM_TEXTURE_REMAP_COLOR_A 0
#define GCM_TEXTURE_REMAP_COLOR_A_SHIFT 0
#define GCM_TEXTURE_REMAP_COLOR_B 3
#define GCM_TEXTURE_REMAP_COLOR_B_SHIFT 6
#define GCM_TEXTURE_REMAP_COLOR_G 2
#define GCM_TEXTURE_REMAP_COLOR_G_SHIFT 4
#define GCM_TEXTURE_REMAP_COLOR_R 1
#define GCM_TEXTURE_REMAP_COLOR_R_SHIFT 2
#define GCM_TEXTURE_REMAP_TYPE_A_SHIFT 8
#define GCM_TEXTURE_REMAP_TYPE_B_SHIFT 14
#define GCM_TEXTURE_REMAP_TYPE_G_SHIFT 12
#define GCM_TEXTURE_REMAP_TYPE_ONE 1
#define GCM_TEXTURE_REMAP_TYPE_REMAP 2
#define GCM_TEXTURE_REMAP_TYPE_R_SHIFT 10
#define GCM_TEXTURE_REMAP_TYPE_ZERO 0
#define GCM_TEXTURE_REPEAT 1
#define GCM_TEXTURE_ZFUNC_LESS 1
#define GCM_TRANSFER_CONVERSION_TRUNCATE 1
#define GCM_TRANSFER_INTERPOLATOR_NEAREST 0 // point sampling
#define GCM_TRANSFER_LOCAL_TO_LOCAL 0
#define GCM_TRANSFER_OPERATION_SRCCOPY 3
#define GCM_TRANSFER_ORIGIN_CORNER 2
#define GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8 3
#define GCM_TRANSFER_SURFACE 0
#define GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8 0xa
#define GCM_TRUE 1
#define GCM_VERTEX_ATTRIB_COLOR0 3
#define GCM_VERTEX_ATTRIB_NORMAL 2
#define GCM_VERTEX_ATTRIB_POS 0
#define GCM_VERTEX_ATTRIB_TEX0 8

typedef struct {u32 *begin,*end,*current; void* callback;} gcmContextData;
#define GCM_TRANSFER_LOCAL_TO_MAIN 2
void rsxSetTransferDataMode(gcmContextData*,u8);
void rsxSetTransferDataFormat(gcmContextData*,s32,s32,u32,u32,u8,u8);
void rsxSetTransferDataOffset(gcmContextData*,u32,u32);
typedef struct {
 u8 type,antiAlias,colorFormat,colorTarget,colorLocation[4];
 u32 colorOffset[4],colorPitch[4]; u8 depthFormat,depthLocation;
 u32 depthOffset,depthPitch;u16 width,height,x,y;
} gcmSurface;
typedef struct {
 u8 format,mipmap,dimension,cubemap;u32 remap;u16 width,height,depth;
 u8 location;u32 pitch,offset;
} gcmTexture;
typedef struct {u32 name_off,index,type;} rsxProgramAttrib;
typedef struct {u32 index;} rsxProgramConst;
typedef struct {u32 num;u32 offset[1];} rsxConstOffsetTable;
typedef struct {u32 unused;} rsxVertexProgram;
typedef struct {u32 unused;} rsxFragmentProgram;
typedef struct {
 u32 conversion,format,origin,operation,interp,clipX,clipY,clipW,clipH;
 u32 outX,outY,outW,outH,ratioX,ratioY,inX,inY,inW,inH,offset,pitch;
} gcmTransferScale;
typedef struct {u32 format,pitch,offset;} gcmTransferSurface;
void* rsxMemalign(size_t,size_t);
void rsxFree(void*);
void rsxAddressToOffset(const void*,u32*);
void rsxSetSurface(gcmContextData*,const gcmSurface*);
void rsxSetViewport(gcmContextData*,u16,u16,u16,u16,f32,f32,const f32*,const f32*);
void rsxSetScissor(gcmContextData*,u16,u16,u16,u16);
void rsxClearSurface(gcmContextData*,u32);
void rsxLoadTexture(gcmContextData*,u8,const gcmTexture*);
void rsxDrawVertexBegin(gcmContextData*,u32);
void rsxSetWriteBackendLabel(gcmContextData*,u8,u32);
void* gcmGetLabelAddress(u8);
static inline void gcmSetWaitFlip(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxDrawVertex2f(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxDrawVertex3f(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxDrawVertex4f(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxDrawVertex4ub(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxDrawVertexEnd(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxFinish(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxFlushBuffer(gcmContextData* unused, ...) {(void)unused;}
void rsxInlineTransfer(gcmContextData*,u32,const void*,u32,u32);
static inline void rsxInvalidateTextureCache(gcmContextData* unused, ...) {(void)unused;}
void rsxLoadFragmentProgramLocation(gcmContextData*,rsxFragmentProgram*,u32,u8);
void rsxLoadVertexProgram(gcmContextData*,rsxVertexProgram*,const void*);
static inline void rsxSetAlphaFunc(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetAlphaTestEnable(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetBackPolygonMode(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetBlendColor(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetBlendEnable(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetBlendEquation(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetBlendFunc(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetClearColor(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetClearDepthStencil(gcmContextData* unused, ...) {(void)unused;}
void rsxSetColorMask(gcmContextData*,u32);
static inline void rsxSetColorMaskMrt(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetCullFace(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetCullFaceEnable(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetDepthFunc(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetDepthTestEnable(gcmContextData* unused, ...) {(void)unused;}
void rsxSetDepthWriteEnable(gcmContextData*,u32);
static inline void rsxSetFogMode(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetFogParams(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetFrontPolygonMode(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetLogicOp(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetLogicOpEnable(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetPointSize(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetShadeModel(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetTransferScaleMode(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxSetTransferScaleSurface(gcmContextData* unused, ...) {(void)unused;}
void rsxSetVertexProgramParameter(gcmContextData*,rsxVertexProgram*,rsxProgramConst*,const float*);
static inline void rsxTextureControl(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxTextureFilter(gcmContextData* unused, ...) {(void)unused;}
static inline void rsxTextureWrapMode(gcmContextData* unused, ...) {(void)unused;}
static inline u32 rsxGetFixedUint16(u32 x) {return x << 4;}
static inline rsxConstOffsetTable* rsxFragmentProgramGetConstOffsetTable(const rsxFragmentProgram* p,u32 i) {
    (void)p;static rsxConstOffsetTable tables[32];if(i>=32)abort();tables[i].num=1;tables[i].offset[0]=i*16;return &tables[i];
}
static inline rsxProgramAttrib* rsxFragmentProgramGetAttrib(const rsxFragmentProgram* p,const char* n) {(void)p;(void)n;return NULL;}
static inline rsxProgramAttrib* rsxFragmentProgramGetAttribs(const rsxFragmentProgram* p) {(void)p;return NULL;}
static inline u16 rsxFragmentProgramGetNumAttrib(const rsxFragmentProgram* p) {(void)p;return 0;}
static inline rsxProgramConst* rsxFragmentProgramGetConst(const rsxFragmentProgram* p,const char* n) {(void)p;(void)n;return NULL;}
static inline rsxProgramConst* rsxVertexProgramGetConst(const rsxVertexProgram* p,const char* n) {(void)p;(void)n;return NULL;}
static inline rsxProgramAttrib* rsxVertexProgramGetAttrib(const rsxVertexProgram* p,const char* n) {(void)p;(void)n;return NULL;}
static inline void rsxVertexProgramGetUCode(const rsxVertexProgram* p,void** b,u32* s) {(void)p;*b=NULL;*s=0;}
static inline void rsxFragmentProgramGetUCode(const rsxFragmentProgram* p,void** b,u32* s) {(void)p;*b=NULL;*s=0;}
#endif
