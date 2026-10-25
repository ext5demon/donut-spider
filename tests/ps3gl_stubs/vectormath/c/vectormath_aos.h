#ifndef TEST_VECTORMATH_H
#define TEST_VECTORMATH_H
#include <string.h>
/* The picker recorder uses identity and column loads. The remaining unused
 * operations only allow the full production translation unit to compile. */
typedef struct {float m[16];} VmathMatrix4;
typedef struct {float v[4];} VmathVector3;
typedef struct {float v[4];} VmathVector4;
static inline void vmathM4AppendScale(void* unused, ...) {(void)unused;}
static inline void vmathM4Copy(VmathMatrix4* out,const VmathMatrix4* in) {*out=*in;}
static inline void vmathM4MakeFromCols(VmathMatrix4* out,const VmathVector4* a,const VmathVector4* b,const VmathVector4* c,const VmathVector4* d) {
    memcpy(out->m,a->v,16);memcpy(out->m+4,b->v,16);memcpy(out->m+8,c->v,16);memcpy(out->m+12,d->v,16);
}
static inline void vmathM4MakeFrustum(void* unused, ...) {(void)unused;}
static inline void vmathM4MakeIdentity(VmathMatrix4* out) {memset(out,0,sizeof(*out));out->m[0]=out->m[5]=out->m[10]=out->m[15]=1;}
static inline void vmathM4MakeOrthographic(void* unused, ...) {(void)unused;}
static inline void vmathM4MakeRotationAxis(void* unused, ...) {(void)unused;}
static inline void vmathM4MakeTranslation(void* unused, ...) {(void)unused;}
static inline void vmathM4Mul(void* unused, ...) {(void)unused;}
static inline void vmathV3MakeFromElems(void* unused, ...) {(void)unused;}
static inline void vmathV3Normalize(void* unused, ...) {(void)unused;}
static inline void vmathV4MakeFromElems(VmathVector4* out,float a,float b,float c,float d) {out->v[0]=a;out->v[1]=b;out->v[2]=c;out->v[3]=d;}
#endif
