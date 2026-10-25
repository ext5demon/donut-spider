#ifndef _BS_PS3GL_H_
#define _BS_PS3GL_H_

#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C"
{
#endif

void ps3glInit(void);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void ps3glSwapBuffers(void);
/* Whether a texture owns valid RSX storage after an upload. */
bool ps3glTextureReady(GLuint id);
/* Print draw state only when a GPU watchdog fires. */
void ps3glLogGpuState(void);

#define PS3GL_SHADER_BINARY_VPO         0x1ED01
#define PS3GL_SHADER_BINARY_FPO         0x1ED02

typedef char GLchar;

#ifdef __cplusplus
}
#endif

#endif /* _BS_PS3GL_H_ */
