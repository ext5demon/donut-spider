// PS3GL - An OpenGL 1.5 Compatibility Layer on top of the RSX API

#include "GL/gl.h"
#include "ps3gl.h"
#include <GL/glext.h>
#include <stdint.h>
#include <unistd.h>
// TODO: Move the rsxutil functionality into ps3glInit
#include "rsxutil.h"

#include "ps3gl_internal.h"
#include "pixel_layout.h"
#include "raster_bounds.h"
#include "ffp_shader_vpo.h"
#include "ffp_shader_fpo.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void PS3SessionLog_event(const char* format, ...);
extern void PS3SessionLog_flushToDisk(void);
extern void PS3SessionLog_trace(const char* format, ...);
#ifdef __cplusplus
}
#endif

#ifndef GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE
#define GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE GCM_TEXTURE_MIRROR_ONCE_CLAMP_TO_EDGE 
#endif

static struct ps3gl_opengl_state _opengl_state;
static gcmTexture _opengl_dummy_texture;
static GLenum ps3glError;
static uint32_t renderTargetRevision;
static Ps3glRasterRect lastSubmittedViewport, lastSubmittedScissor;
static void setError(GLenum error) { if (ps3glError == GL_NO_ERROR) ps3glError = error; }

static inline struct ps3gl_texture* _ps3gl_active_bound_tex(void)
{
	return _opengl_state.bound_textures[_opengl_state.active_texture_unit];
}

void ps3glLogGpuState(void) {
    struct ps3gl_framebuffer* fb = _opengl_state.bound_draw_framebuffer;
    struct ps3gl_texture* texture = _opengl_state.bound_textures[0];
    PS3SessionLog_event("RSX_STATE: draw_fbo=%u target=%ux%u program=%u texture=%u texture_size=%ux%u",
        fb ? fb->id : 0, fb ? fb->gcmSurface.width : display_width,
        fb ? fb->gcmSurface.height : display_height, _opengl_state.active_program,
        texture ? texture->id : 0, texture ? texture->gcmTexture.width : 0,
        texture ? texture->gcmTexture.height : 0);
    PS3SessionLog_event("RSX_STATE: GL_viewport=(%d,%d %dx%d) submitted=(%u,%u %ux%u) scissor=(%u,%u %ux%u)",
        _opengl_state.viewport.x, _opengl_state.viewport.y, _opengl_state.viewport.w, _opengl_state.viewport.h,
        lastSubmittedViewport.x, lastSubmittedViewport.y, lastSubmittedViewport.w, lastSubmittedViewport.h,
        lastSubmittedScissor.x, lastSubmittedScissor.y, lastSubmittedScissor.w, lastSubmittedScissor.h);
}

u32 fp_offset;
u32 *fp_buffer;

void *vp_ucode = NULL;
void *fp_ucode = NULL;

rsxVertexProgram *vpo = (rsxVertexProgram*)ffp_shader_vpo;
rsxFragmentProgram *fpo = (rsxFragmentProgram*)ffp_shader_fpo;

/*
 * Miscellaneous
 */

void glClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha )
{
	_opengl_state.clear_color = 
		(((uint8_t)(alpha * 255.0f)) << 24) |
		(((uint8_t)(red   * 255.0f)) << 16) |
		(((uint8_t)(green * 255.0f)) << 8)  |
		(((uint8_t)(blue  * 255.0f)) << 0);
		
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("GPU: glClearColor(%f, %f, %f, %f) -> %08X", (float)red, (float)green, (float)blue, (float)alpha, _opengl_state.clear_color);
	}
}

// from mesa's nouveau driver, specifically nv30/nv30_clear.c
static inline uint32_t
pack_zeta(bool use_stencil, double depth, unsigned stencil)
{
   uint32_t zuint = (uint32_t)(depth * UINT32_MAX);
    if (use_stencil)
 	  return (zuint & 0xffffff00) | (stencil & 0xff);
   return zuint >> 16;
}


void _setup_draw_env(void);
static void _ps3gl_setup_render_target(void);
static void _ps3gl_setup_scissor(void);
void glClear( GLbitfield mask )
{
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("GPU: glClear(0x%X)", mask);
	}
	// A clear ignores the GL viewport, textures and shaders. Step events can
	// leave a screen-sized viewport on a small application surface; loading
	// that as unsigned RSX coordinates here used to hang the next frame.
	_ps3gl_setup_render_target();
	_ps3gl_setup_scissor();
	rsxSetColorMask(context, _opengl_state.color_mask);
	rsxSetColorMaskMrt(context, 0);
	rsxSetDepthWriteEnable(context, _opengl_state.depth_mask);
	uint32_t rsx_mask = 0;

	if(mask == (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT))
	{
		rsx_mask = GCM_CLEAR_M;
		rsxSetClearColor(context, _opengl_state.clear_color);
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsxClearSurface(context, rsx_mask);
		return;
	} 

	if(mask & GL_COLOR_BUFFER_BIT)
	{
		rsx_mask |= (GCM_CLEAR_A | GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B);
		rsxSetClearColor(context, _opengl_state.clear_color);
	}
	
	if(mask & GL_DEPTH_BUFFER_BIT)
	{
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsx_mask |= GCM_CLEAR_Z;
	}
	
	if(mask & GL_STENCIL_BUFFER_BIT)
	{
		rsxSetClearDepthStencil(context, pack_zeta(true, _opengl_state.clear_depth, _opengl_state.clear_stencil));
		rsx_mask |= GCM_CLEAR_S;
	}

	rsxClearSurface(context, rsx_mask);
}

void glColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha )
{
	_opengl_state.color_mask = 0;
	if(red)   _opengl_state.color_mask |= GCM_COLOR_MASK_R;
	if(green) _opengl_state.color_mask |= GCM_COLOR_MASK_G;
	if(blue)  _opengl_state.color_mask |= GCM_COLOR_MASK_B;
	if(alpha) _opengl_state.color_mask |= GCM_COLOR_MASK_A;
}

void glAlphaFunc( GLenum func, GLclampf ref )
{
	_opengl_state.alpha_func = func;
	_opengl_state.alpha_func_ref = ref * 255;
}

void glBlendFunc( GLenum sfactor, GLenum dfactor )
{
	_opengl_state.blend_func_sfactor = sfactor;
	_opengl_state.blend_func_dfactor = dfactor;
	_opengl_state.blend_func_sfactor_alpha = sfactor;
	_opengl_state.blend_func_dfactor_alpha = dfactor;
}

void glBlendFuncSeparate( GLenum sfactor, GLenum dfactor, GLenum sfactor_alpha, GLenum dfactor_alpha )
{
	_opengl_state.blend_func_sfactor = sfactor;
	_opengl_state.blend_func_dfactor = dfactor;
	_opengl_state.blend_func_sfactor_alpha = sfactor_alpha;
	_opengl_state.blend_func_dfactor_alpha = dfactor_alpha;
}

void glLogicOp( GLenum opcode )
{
	_opengl_state.logic_op = opcode;
}

void glCullFace( GLenum mode )
{
	_opengl_state.cull_face = mode;
}

void glFrontFace( GLenum mode )
{
	// The Enum values match BUT 
	// due to how the RSX Works GL_CW == GCM_CCW and GL_CCW == GL_CW
	_opengl_state.front_face = mode ^ 1u;
}

void glPointSize( GLfloat size )
{
	_opengl_state.point_size = size;	
}

void glPolygonMode( GLenum face, GLenum mode )
{
	_opengl_state.polygon_mode_face = face;
	_opengl_state.polygon_mode = mode;
}

void glScissor( GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
	_opengl_state.scissor.x = x;
	_opengl_state.scissor.y = y;
	_opengl_state.scissor.w = width;
	_opengl_state.scissor.h = height;
}

void glEnable( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			_opengl_state.alpha_test_enabled = true;
			break;
		case GL_BLEND:
			_opengl_state.blend_enabled = true;
			break;
		case GL_CULL_FACE:
			_opengl_state.cull_face_enabled = true;
			break;
		case GL_COLOR_LOGIC_OP:
			_opengl_state.logic_op_enabled = true;
			break;
		case GL_DEPTH_TEST:
			_opengl_state.depth_test = true;
			break;
		case GL_FOG:
			_opengl_state.fog_enabled = true;
			break;
		case GL_TEXTURE_2D:
			_opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit] = true;
			break;
		case GL_SCISSOR_TEST:
			_opengl_state.scissor.enabled = true;
			break;
		default:
			break;
	}
}

GLboolean glIsEnabled( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			return _opengl_state.alpha_test_enabled;
		case GL_BLEND:
			return _opengl_state.blend_enabled;
		case GL_CULL_FACE:
			return _opengl_state.cull_face_enabled;
		case GL_COLOR_LOGIC_OP:
			return _opengl_state.logic_op_enabled;
		case GL_DEPTH_TEST:
			return _opengl_state.depth_test;
		case GL_FOG:
			return _opengl_state.fog_enabled;
		case GL_TEXTURE_2D:
			return _opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit];
		case GL_SCISSOR_TEST:
			return _opengl_state.scissor.enabled;
		default:
			return false;
	}
}

void glDisable( GLenum cap )
{
	switch(cap)
	{
		case GL_ALPHA_TEST:
			_opengl_state.alpha_test_enabled = false;
			break;
		case GL_BLEND:
			_opengl_state.blend_enabled = false;
			break;
		case GL_CULL_FACE:
			_opengl_state.cull_face_enabled = false;
			break;
		case GL_COLOR_LOGIC_OP:
			_opengl_state.logic_op_enabled = false;
			break;
		case GL_DEPTH_TEST:
			_opengl_state.depth_test = false;
			break;
		case GL_FOG:
			_opengl_state.fog_enabled = false;
			break;
		case GL_TEXTURE_2D:
			_opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit] = false;
			break;
		case GL_SCISSOR_TEST:
			_opengl_state.scissor.enabled = false;
			break;
		default:
			break;
	}
}

GLenum glGetError(void) { GLenum result = ps3glError; ps3glError = GL_NO_ERROR; return result; }

static u32 finish_ref_value = 1;
static void safe_rsxFinish(gcmContextData *ctx) {
    rsxSetWriteBackendLabel(ctx, 254, finish_ref_value);
    rsxFlushBuffer(ctx);
    
    int retries = 0;
    while(*(vu32*)gcmGetLabelAddress(254) != finish_ref_value) {
        usleep(30);
        retries++;
        if (retries > 100000) { // ~3 seconds
            PS3SessionLog_event("RSX_PANIC: GPU hung during glFinish! (ref_value=%d)", finish_ref_value);
            ps3glLogGpuState();
            extern void PS3SessionLog_close(const char*);
            PS3SessionLog_close("GPU HUNG");
            exit(1); 
        }
    }
    finish_ref_value++;
}

void glFinish( void ) { safe_rsxFinish(context); } // Force GPU to execute all queued commands

void glFlush( void ) {} // We call rsxFlushBuffer every frame

void glHint( GLenum target, GLenum mode ) {} // No idea how to implement this

/*
 * Depth Buffer
 */

void glClearDepth( GLclampd depth )
{
	_opengl_state.clear_depth = depth;	
}

void glDepthFunc( GLenum func )
{
	// Values is the same between what OpenGL defines 
	// and what the RSX expects
	_opengl_state.depth_func = func;
}

void glDepthMask( GLboolean flag )
{
	_opengl_state.depth_mask = flag;
}

void glDepthRange( GLclampd near_val, GLclampd far_val )
{
	_opengl_state.depth_near = near_val;
	_opengl_state.depth_far  = far_val;
}

/*
 * Transformation
 */


void glMatrixMode( GLenum mode )
{
	_opengl_state.matrix_mode = mode;
	switch(mode)
	{
		case GL_MODELVIEW:
			_opengl_state.curr_mtx = &_opengl_state.modelview_matrix;
			break;
		case GL_PROJECTION:
			_opengl_state.curr_mtx = &_opengl_state.projection_matrix;
			break;
		default:
			fprintf(stderr, "Unimplemented MatrixMode: %u", mode);
			break;
	}
}

// glPopMatrix and glPushMatrix are based on OpenGX
void glPopMatrix(void)
{
    switch (_opengl_state.matrix_mode) {
    case GL_PROJECTION:
        memcpy(&_opengl_state.projection_matrix, &_opengl_state.projection_stack[_opengl_state.cur_proj_mat], sizeof(VmathMatrix4));
        _opengl_state.cur_proj_mat--;
		
        break;
    case GL_MODELVIEW:
        memcpy(&_opengl_state.modelview_matrix, &_opengl_state.modelview_stack[_opengl_state.cur_modv_mat], sizeof(VmathMatrix4));
        _opengl_state.cur_modv_mat--;
        break;
    default:
        break;
    }
    //glparamstate.dirty.bits.dirty_matrices = 1;
}

void glPushMatrix(void)
{
    switch (_opengl_state.matrix_mode) {
    case GL_PROJECTION:
        _opengl_state.cur_proj_mat++;
        memcpy(&_opengl_state.projection_stack[_opengl_state.cur_proj_mat], &_opengl_state.projection_matrix, sizeof(VmathMatrix4));
        break;
    case GL_MODELVIEW:
        _opengl_state.cur_modv_mat++;
        memcpy(&_opengl_state.modelview_stack[_opengl_state.cur_modv_mat], &_opengl_state.modelview_matrix, sizeof(VmathMatrix4));
        break;
    default:
        break;
    }
}

void glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                                   GLdouble near_val, GLdouble far_val )
{
  	VmathMatrix4 ortho;
  	vmathM4MakeOrthographic(&ortho, (GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
  	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &ortho);
  	vmathM4Copy(_opengl_state.curr_mtx, &result);
	
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("GPU: glOrtho(%f, %f, %f, %f, %f, %f)", (float)left, (float)right, (float)bottom, (float)top, (float)near_val, (float)far_val);
	}
}

void glFrustum( GLdouble left, GLdouble right,
                                   GLdouble bottom, GLdouble top,
                                   GLdouble near_val, GLdouble far_val )
{
	VmathMatrix4 frustum;
	vmathM4MakeFrustum(&frustum, (GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
	
	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &frustum);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glViewport( GLint x, GLint y, GLsizei width, GLsizei height )
{
	if (width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
	// Keep signed GL coordinates. Conversion and clipping belong at submission,
	// because binding another FBO can change the target height after this call.
	_opengl_state.viewport.x = x;
	_opengl_state.viewport.y = y;
	_opengl_state.viewport.w = width;
	_opengl_state.viewport.h = height;
}

void glPushMatrix(void); // TODO

void glPopMatrix(void); // TODO

void glLoadIdentity(void)
{
	vmathM4MakeIdentity(_opengl_state.curr_mtx);
}

void glLoadMatrixf( const GLfloat *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, m[0],  m[1],  m[2],  m[3]);
	vmathV4MakeFromElems(&col1, m[4],  m[5],  m[6],  m[7]);
	vmathV4MakeFromElems(&col2, m[8],  m[9],  m[10], m[11]);
	vmathV4MakeFromElems(&col3, m[12], m[13], m[14], m[15]);
	
	vmathM4MakeFromCols(_opengl_state.curr_mtx, &col0, &col1, &col2, &col3);
}


void glLoadMatrixd( const GLdouble *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, (GLfloat)m[0],  (GLfloat)m[1],  (GLfloat)m[2],  (GLfloat)m[3]);
	vmathV4MakeFromElems(&col1, (GLfloat)m[4],  (GLfloat)m[5],  (GLfloat)m[6],  (GLfloat)m[7]);
	vmathV4MakeFromElems(&col2, (GLfloat)m[8],  (GLfloat)m[9],  (GLfloat)m[10], (GLfloat)m[11]);
	vmathV4MakeFromElems(&col3, (GLfloat)m[12], (GLfloat)m[13], (GLfloat)m[14], (GLfloat)m[15]);
	
	vmathM4MakeFromCols(_opengl_state.curr_mtx, &col0, &col1, &col2, &col3);
}

void glMultMatrixf( const GLfloat *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;

	vmathV4MakeFromElems(&col0, m[0],  m[1],  m[2],  m[3]);
	vmathV4MakeFromElems(&col1, m[4],  m[5],  m[6],  m[7]);
	vmathV4MakeFromElems(&col2, m[8],  m[9],  m[10], m[11]);
	vmathV4MakeFromElems(&col3, m[12], m[13], m[14], m[15]);

	VmathMatrix4 mulMatrix __attribute__((aligned(16)));
	vmathM4MakeFromCols(&mulMatrix, &col0, &col1, &col2, &col3);

	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &mulMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glMultMatrixd( const GLdouble *m )
{
	VmathVector4 col0;
	VmathVector4 col1;
	VmathVector4 col2;
	VmathVector4 col3;
	
	vmathV4MakeFromElems(&col0, (GLfloat)m[0],  (GLfloat)m[1],  (GLfloat)m[2],  (GLfloat)m[3]);
	vmathV4MakeFromElems(&col1, (GLfloat)m[4],  (GLfloat)m[5],  (GLfloat)m[6],  (GLfloat)m[7]);
	vmathV4MakeFromElems(&col2, (GLfloat)m[8],  (GLfloat)m[9],  (GLfloat)m[10], (GLfloat)m[11]);
	vmathV4MakeFromElems(&col3, (GLfloat)m[12], (GLfloat)m[13], (GLfloat)m[14], (GLfloat)m[15]);
	
	VmathMatrix4 mulMatrix __attribute__((aligned(16)));
	vmathM4MakeFromCols(&mulMatrix, &col0, &col1, &col2, &col3);

	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &mulMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 unitVec;
	vmathV3MakeFromElems(&unitVec, x, y, z);
	vmathV3Normalize(&unitVec, &unitVec);

	VmathMatrix4 rotation;
	vmathM4MakeRotationAxis(&rotation, (M_PI/180)*angle, &unitVec);

	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &rotation);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glRotated( GLdouble angle, GLdouble x, GLdouble y, GLdouble z )
{
	glRotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void glScalef( GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 scale;
	vmathV3MakeFromElems(&scale, x, y, z);

	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4AppendScale(&result, _opengl_state.curr_mtx, &scale);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glScaled( GLdouble x, GLdouble y, GLdouble z )
{
	glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}


void glTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
	VmathVector3 translation;
	vmathV3MakeFromElems(&translation, x, y, z);
	
    VmathMatrix4 translationMatrix __attribute__((aligned(16)));
	vmathM4MakeIdentity(&translationMatrix);
    vmathM4MakeTranslation(&translationMatrix, &translation);

	VmathMatrix4 result __attribute__((aligned(16)));
	vmathM4Mul(&result, _opengl_state.curr_mtx, &translationMatrix);
	vmathM4Copy(_opengl_state.curr_mtx, &result);
}

void glTranslated( GLdouble x, GLdouble y, GLdouble z )
{
	glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}


/*
 * Drawing Functions
 */
 
int g_vert_count = 0;
void glBegin(GLenum mode)
{
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("glBegin called with mode %d", mode);
	}
	g_vert_count = 0;
	_setup_draw_env();
	rsxDrawVertexBegin(context, mode+1);
}

void glEnd(void)
{
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("glEnd called with %d vertices", g_vert_count);
	}
	rsxDrawVertexEnd(context);

	
	// Paranoid validation for command buffer alignment
	if (((uintptr_t)context->current) % 4 != 0) {
		PS3SessionLog_event("RSX_PANIC: unaligned pointer after glEnd!");
		PS3SessionLog_flushToDisk();
		exit(1);
	}
}

uint32_t g_vpo_aPos = GCM_VERTEX_ATTRIB_POS;
uint32_t g_vpo_aColor = GCM_VERTEX_ATTRIB_COLOR0;
uint32_t g_vpo_aTexCoord0 = GCM_VERTEX_ATTRIB_TEX0;

void glVertex2f(GLfloat x, GLfloat y)
{
	g_vert_count++;
	GLfloat v[2] = {x,y};
	rsxDrawVertex2f(context, g_vpo_aPos, v);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	g_vert_count++;
	GLfloat v[3] = {x,y,z};
	rsxDrawVertex3f(context, g_vpo_aPos, v);
}

void glVertex3fv(const GLfloat *v)
{
	g_vert_count++;
	rsxDrawVertex3f(context, g_vpo_aPos, (float*)v);
}

void glNormal3f( GLfloat nx, GLfloat ny, GLfloat nz )
{
	// Stubbed until Lighting is added
#if 0
	GLfloat v[3] = {nx,ny,nz};
	rsxDrawVertex3f(context, GCM_VERTEX_ATTRIB_NORMAL, v);
#endif
}

void glColor3f( GLfloat red, GLfloat green,
                                   GLfloat blue)
{
	GLfloat v[4] = {red,green,blue,1.0f};
	rsxDrawVertex4f(context, g_vpo_aColor, v);
}

void glColor4f( GLfloat red, GLfloat green,
                                   GLfloat blue, GLfloat alpha )
{
	GLfloat v[4] = {red,green,blue,alpha};
	rsxDrawVertex4f(context, g_vpo_aColor, v);
}

void glColor4ub( GLubyte red, GLubyte green,
                                   GLubyte blue, GLubyte alpha ){
	GLubyte v[4] = {red,green,blue,alpha};
	rsxDrawVertex4ub(context, GCM_VERTEX_ATTRIB_COLOR0, v);
}

void glColor4fv(const GLfloat * v)
{
	rsxDrawVertex4f(context, g_vpo_aColor, v);
}

void glColor4ubv(const GLubyte * v)
{
	GLfloat f[4];
	
	f[0] = (GLfloat)v[0] / 255.0f;
	f[1] = (GLfloat)v[1] / 255.0f;
	f[2] = (GLfloat)v[2] / 255.0f;
	f[3] = (GLfloat)v[3] / 255.0f;
	
	rsxDrawVertex4f(context, g_vpo_aColor, f);
}


void glTexCoord2f(GLfloat s, GLfloat t)
{
	GLfloat v[2] = {s,t};
	rsxDrawVertex2f(context, g_vpo_aTexCoord0, v);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	GLfloat v[4] = {s,t,r,q};
	rsxDrawVertex4f(context, g_vpo_aTexCoord0, v);
}

/*
 * Lighting
 */
 void glShadeModel( GLenum mode )
 {
	switch(mode)
	{
		case GL_FLAT:
			_opengl_state.shade_model = GCM_SHADE_MODEL_FLAT;
			break;
		case GL_SMOOTH:
			_opengl_state.shade_model = GCM_SHADE_MODEL_SMOOTH;
			break;
 	}
 }

/*
 * Stenciling
 */
void glClearStencil(GLint s)
{
	_opengl_state.clear_stencil = s;
}

/*
 * Texture mapping
 */

void glTexEnvi( GLenum target, GLenum pname, GLint param )
{
	switch(target)
	{
		case GL_TEXTURE_ENV:
			switch(pname)
			{
				case GL_TEXTURE_ENV_MODE:
					switch(param)
					{
						case GL_REPLACE:
						_opengl_state.texEnvMode = PS3GL_TEXENV_REPLACE;
						break;
						case GL_MODULATE:
						_opengl_state.texEnvMode = PS3GL_TEXENV_MODULATE;
						break;
						case GL_BLEND:
						_opengl_state.texEnvMode = PS3GL_TEXENV_BLEND;
						break;
					}
					break;
				default:
					break;
			}
		default:
			break;
	}
}

void glTexEnvf( GLenum target, GLenum pname, GLfloat param )
{
	switch(target)
	{
		case GL_TEXTURE_ENV:
			switch(pname)
			{
				default:
					glTexEnvi(target, pname, (GLint)param);
					break;
			}
		default:
			break;
	}
}

void glTexParameteri( GLenum target, GLenum pname, GLint param )
{
		switch(pname)
		{
			case GL_TEXTURE_MIN_FILTER:
				switch(param)
				{
					case GL_NEAREST:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_NEAREST;
						break;
					case GL_LINEAR:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_LINEAR;
						break;
					case GL_NEAREST_MIPMAP_NEAREST:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_NEAREST;
						break;
					case GL_LINEAR_MIPMAP_NEAREST:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
						break;
					case GL_NEAREST_MIPMAP_LINEAR:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
						break;
					case GL_LINEAR_MIPMAP_LINEAR:
						_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
						break;
					
				}
				break;
		case GL_TEXTURE_MAG_FILTER:
			if(param == GL_NEAREST) 
				_ps3gl_active_bound_tex()->magFilter = GCM_TEXTURE_NEAREST;
			if(param == GL_LINEAR) 
				_ps3gl_active_bound_tex()->magFilter = GCM_TEXTURE_LINEAR;
			break;
		case GL_TEXTURE_WRAP_S:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
				}
			break;
		case GL_TEXTURE_WRAP_T:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
			}
			break;
		case GL_TEXTURE_WRAP_R:
			switch(param)
			{
				case GL_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_CLAMP_TO_EDGE;
					break;
				case GL_CLAMP_TO_BORDER:
					_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_CLAMP_TO_BORDER;
					break;
				case GL_MIRRORED_REPEAT:
					_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_MIRRORED_REPEAT;
					break;
				case GL_REPEAT:
					_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_REPEAT;
					break;
				case GL_MIRROR_CLAMP_TO_EDGE:
					_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_MIRROR_CLAMP_TO_EDGE;
					break;
			}
			break;
	}
}
void glTexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	switch (pname) {
		default:
			glTexParameteri(target, pname, (GLint)param);
			break;
	}
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    (void)internalFormat;
    struct ps3gl_texture* texture = _ps3gl_active_bound_tex();
    if (!texture || texture->id == 0) { setError(GL_INVALID_OPERATION); return; }
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE ||
        (format != GL_RGBA && format != GL_RGB && format != GL_RED)) {
        setError(GL_INVALID_ENUM); return;
    }
    if (level != 0 || border != 0 || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        setError(GL_INVALID_VALUE); return;
    }
    unsigned components = format == GL_RED ? 1 : (format == GL_RGB ? 3 : 4);
    size_t pitch = ps3glPixelPitch((unsigned)width, components == 1 ? 1 : 4);
    size_t bytes = pitch * (size_t)height;
    uint8_t* data = (uint8_t*)rsxMemalign(128, bytes);
    if (!data) {
        setError(GL_OUT_OF_MEMORY);
        PS3SessionLog_event("RSX texture allocation failed: %dx%d bytes=%u", width, height, (unsigned)bytes);
        return;
    }
    memset(data, 0, bytes);
    if (pixels) ps3glUploadPixels(data, pitch, pixels, width, height, components);
    if (texture->data) { glFinish(); rsxFree(texture->data); }
    texture->data = data;
    texture->gcmTexture.width = width;
    texture->gcmTexture.height = height;
    texture->gcmTexture.depth = 1;
    texture->gcmTexture.mipmap = 1;
    texture->gcmTexture.dimension = GCM_TEXTURE_DIMS_2D;
    texture->gcmTexture.location = GCM_LOCATION_RSX;
    texture->gcmTexture.pitch = pitch;
    rsxAddressToOffset(data, &texture->gcmTexture.offset);
    texture->gcmTexture.format = (components == 1 ? GCM_TEXTURE_FORMAT_B8 : GCM_TEXTURE_FORMAT_A8R8G8B8) | GCM_TEXTURE_FORMAT_LIN;
    if (components == 1) {
        texture->gcmTexture.remap =
            (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_ZERO << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_ZERO << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_ONE << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
            (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_R_SHIFT);
    } else {
        texture->gcmTexture.remap =
            (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
            (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
            (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT) |
            (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
            (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
            (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT);
    }
    rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
}

bool ps3glTextureReady(GLuint id) {
    return id > 0 && id < MAX_TEXTURES && _opengl_state.textures[id].allocated && _opengl_state.textures[id].data != NULL;
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid* pixels) {
    struct ps3gl_texture* texture = _ps3gl_active_bound_tex();
    if (!texture || !texture->data || !pixels) { setError(GL_INVALID_OPERATION); return; }
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE ||
        (format != GL_RGBA && format != GL_RGB && format != GL_RED)) { setError(GL_INVALID_ENUM); return; }
    if (level != 0 || width < 0 || height < 0 || xoffset < 0 || yoffset < 0 ||
        (uint64_t)xoffset + width > texture->gcmTexture.width ||
        (uint64_t)yoffset + height > texture->gcmTexture.height) { setError(GL_INVALID_VALUE); return; }
    bool indexTexture = (texture->gcmTexture.format & ~GCM_TEXTURE_FORMAT_LIN) == GCM_TEXTURE_FORMAT_B8;
    if (indexTexture != (format == GL_RED)) { setError(GL_INVALID_OPERATION); return; }
    if (width == 0 || height == 0) return;
    /* The previous frame may still be sampling a streamed video surface. */
    glFinish();
    uint8_t* dst = texture->data + (size_t)yoffset * texture->gcmTexture.pitch + (size_t)xoffset * (indexTexture ? 1 : 4);
    ps3glUploadPixels(dst, texture->gcmTexture.pitch, pixels, width, height, format == GL_RED ? 1 : (format == GL_RGB ? 3 : 4));
    rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
}

static void initTexture(GLuint id) {
    struct ps3gl_texture* texture = &_opengl_state.textures[id];
    memset(texture, 0, sizeof(*texture));
    texture->id = id;
    texture->allocated = true;
    texture->target = GL_TEXTURE_2D;
    texture->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
    texture->magFilter = GCM_TEXTURE_LINEAR;
    texture->wrapS = texture->wrapT = texture->wrapR = GCM_TEXTURE_REPEAT;
}

void glGenTextures(GLsizei n, GLuint* textures) {
    if (!textures || n < 0) { setError(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        GLuint id;
        for (id = 1; id < MAX_TEXTURES && _opengl_state.textures[id].allocated; ++id) {}
        textures[i] = id < MAX_TEXTURES ? id : 0;
        if (id < MAX_TEXTURES) initTexture(id);
        else setError(GL_OUT_OF_MEMORY);
    }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    if (!textures || n < 0) { setError(GL_INVALID_VALUE); return; }
    bool synchronized = false;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint id = textures[i];
        if (id == 0 || id >= MAX_TEXTURES || !_opengl_state.textures[id].allocated) continue;
        struct ps3gl_texture* texture = &_opengl_state.textures[id];
        if (texture->data) {
            if (!synchronized) { glFinish(); synchronized = true; }
            rsxFree(texture->data);
        }
        for (unsigned u = 0; u < MAX_TEX_UNITS; ++u) {
            if (_opengl_state.bound_textures[u] == texture) {
                _opengl_state.bound_textures[u] = &_opengl_state.textures[0];
                _opengl_state.texture_unit_enabled[u] = false;
            }
        }
        for (unsigned f = 1; f < MAX_FRAMEBUFFERS; ++f) {
            if (_opengl_state.framebuffers[f].fbTexture == texture) {
                _opengl_state.framebuffers[f].fbTexture = NULL;
                ++renderTargetRevision;
            }
        }
        memset(texture, 0, sizeof(*texture));
    }
}

void glGenFramebuffers( GLsizei n, GLuint *framebuffers )
{
	if(framebuffers == NULL || n <= 0)
		return;

	for(size_t i = 0; i < n; i++)
	{
		GLuint id;
        for (id = 1; id < MAX_FRAMEBUFFERS && _opengl_state.framebuffers[id].allocated; ++id) {}
        framebuffers[i] = id < MAX_FRAMEBUFFERS ? id : 0;
        if (id == MAX_FRAMEBUFFERS) { setError(GL_OUT_OF_MEMORY); continue; }
        memset(&_opengl_state.framebuffers[id], 0, sizeof(_opengl_state.framebuffers[id]));
		if(id < MAX_FRAMEBUFFERS)
		{
			_opengl_state.framebuffers[id].id = id;
			_opengl_state.framebuffers[id].allocated = true;
			_opengl_state.framebuffers[id].gcmSurface.colorFormat		= GCM_SURFACE_A8R8G8B8;
			_opengl_state.framebuffers[id].gcmSurface.colorTarget		= GCM_SURFACE_TARGET_0;
			_opengl_state.framebuffers[id].gcmSurface.colorLocation[0]	= GCM_LOCATION_RSX;
			_opengl_state.framebuffers[id].gcmSurface.colorOffset[0]	= 0;
			_opengl_state.framebuffers[id].gcmSurface.colorPitch[0]	= 64;

			_opengl_state.framebuffers[id].gcmSurface.colorLocation[1]	= GCM_LOCATION_RSX;
			_opengl_state.framebuffers[id].gcmSurface.colorLocation[2]	= GCM_LOCATION_RSX;
			_opengl_state.framebuffers[id].gcmSurface.colorLocation[3]	= GCM_LOCATION_RSX;
			_opengl_state.framebuffers[id].gcmSurface.colorOffset[1]	= 0;
			_opengl_state.framebuffers[id].gcmSurface.colorOffset[2]	= 0;
			_opengl_state.framebuffers[id].gcmSurface.colorOffset[3]	= 0;
			_opengl_state.framebuffers[id].gcmSurface.colorPitch[1]	= 64;
			_opengl_state.framebuffers[id].gcmSurface.colorPitch[2]	= 64;
			_opengl_state.framebuffers[id].gcmSurface.colorPitch[3]	= 64;

			_opengl_state.framebuffers[id].gcmSurface.depthFormat	= GCM_SURFACE_ZETA_Z24S8;
			_opengl_state.framebuffers[id].gcmSurface.depthLocation	= GCM_LOCATION_RSX;
			_opengl_state.framebuffers[id].gcmSurface.depthOffset	= 0;
			_opengl_state.framebuffers[id].gcmSurface.depthPitch	= 64;
			_opengl_state.framebuffers[id].depthData				= NULL;
			_opengl_state.framebuffers[id].depthSize				= 0;

			_opengl_state.framebuffers[id].gcmSurface.type			= GCM_SURFACE_TYPE_LINEAR;
			_opengl_state.framebuffers[id].gcmSurface.antiAlias		= GCM_SURFACE_CENTER_1;

			_opengl_state.framebuffers[id].gcmSurface.width			= 0;
			_opengl_state.framebuffers[id].gcmSurface.height		= 0;
			_opengl_state.framebuffers[id].gcmSurface.x				= 0;
			_opengl_state.framebuffers[id].gcmSurface.y				= 0;
		}
	}
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    if (!framebuffers || n < 0) { setError(GL_INVALID_VALUE); return; }
    bool synchronized = false;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint id = framebuffers[i];
        if (id == 0 || id >= MAX_FRAMEBUFFERS) continue;
        struct ps3gl_framebuffer* fb = &_opengl_state.framebuffers[id];
        if (!fb->allocated) continue;
        if (fb->depthData) {
            if (!synchronized) { glFinish(); synchronized = true; }
            rsxFree(fb->depthData);
        }
        if (_opengl_state.bound_read_framebuffer == fb) _opengl_state.bound_read_framebuffer = NULL;
        if (_opengl_state.bound_draw_framebuffer == fb) _opengl_state.bound_draw_framebuffer = NULL;
        memset(fb, 0, sizeof(*fb));
        ++renderTargetRevision;
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (framebuffer >= MAX_FRAMEBUFFERS || (framebuffer && !_opengl_state.framebuffers[framebuffer].allocated)) {
        setError(GL_INVALID_OPERATION); return;
    }
    struct ps3gl_framebuffer* fb = framebuffer ? &_opengl_state.framebuffers[framebuffer] : NULL;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) _opengl_state.bound_read_framebuffer = fb;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        // A recently rendered attachment may already be bound for sampling.
        // Binding-descriptor caches must not keep its old texture-cache data.
        if (_opengl_state.bound_draw_framebuffer != fb) rsxInvalidateTextureCache(context,GCM_INVALIDATE_TEXTURE);
        _opengl_state.bound_draw_framebuffer = fb;
    }
}

GLenum glCheckFramebufferStatus(GLenum target);

GLAPI void APIENTRY glBlitFramebuffer (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
{
  if (!(mask & GL_COLOR_BUFFER_BIT)) return;
  struct ps3gl_framebuffer* read = _opengl_state.bound_read_framebuffer;
  struct ps3gl_framebuffer* draw = _opengl_state.bound_draw_framebuffer;
  int readW = read ? read->gcmSurface.width : display_width;
  int readH = read ? read->gcmSurface.height : display_height;
  int drawW = draw ? draw->gcmSurface.width : display_width;
  int drawH = draw ? draw->gcmSurface.height : display_height;
  if ((read && glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ||
      (draw && glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)) {
      setError(GL_INVALID_OPERATION); return;
  }
  if ((int64_t)srcX1 - srcX0 == (int64_t)dstX1 - dstX0 &&
      (int64_t)srcY1 - srcY0 == (int64_t)dstY1 - dstY0) {
      if (!ps3glClipCopyAxis(&srcX0, &srcX1, &dstX0, &dstX1, readW, drawW) ||
          !ps3glClipCopyAxis(&srcY0, &srcY1, &dstY0, &dstY1, readH, drawH)) return;
  }
  if (srcX0 < 0 || srcY0 < 0 || dstX0 < 0 || dstY0 < 0 ||
      srcX1 <= srcX0 || srcY1 <= srcY0 || dstX1 <= dstX0 || dstY1 <= dstY0 ||
      srcX1 > readW || srcY1 > readH || dstX1 > drawW || dstY1 > drawH) {
      setError(GL_INVALID_VALUE); return;
  }
  gcmTransferScale scale = {0};
  gcmTransferSurface surface = {0};

  GLint dstW = dstX1-dstX0;
  GLint dstH = dstY1-dstY0;

  GLint srcW = srcX1-srcX0;
  GLint srcH = srcY1-srcY0;

  scale.conversion = GCM_TRANSFER_CONVERSION_TRUNCATE;
  scale.format = GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8;
  scale.origin = GCM_TRANSFER_ORIGIN_CORNER;
  scale.operation = GCM_TRANSFER_OPERATION_SRCCOPY;
  scale.interp = GCM_TRANSFER_INTERPOLATOR_NEAREST;
  scale.clipX = dstX0;
  scale.clipY = dstY0;
  scale.clipW = dstW;
  scale.clipH = dstH;
  scale.outX = dstX0;
  scale.outY = dstY0;
  scale.outW = dstW;
  scale.outH = dstH;
  scale.ratioX = ((int64_t)srcW << 20) / dstW;
  scale.ratioY = ((int64_t)srcH << 20) / dstH;
  scale.inX = rsxGetFixedUint16(srcX0);
  scale.inY = rsxGetFixedUint16(srcY0);
  scale.inW = readW;
  scale.inH = readH;
  scale.offset = _opengl_state.bound_read_framebuffer != NULL ? 
				_opengl_state.bound_read_framebuffer->gcmSurface.colorOffset[0] :
				color_offset[curr_fb];
  scale.pitch = _opengl_state.bound_read_framebuffer != NULL ? 
				_opengl_state.bound_read_framebuffer->gcmSurface.colorPitch[0] :
				color_pitch;

  surface.format = GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8;
  surface.pitch = _opengl_state.bound_draw_framebuffer != NULL ? 
				_opengl_state.bound_draw_framebuffer->gcmSurface.colorPitch[0] :
				color_pitch;
  surface.offset = _opengl_state.bound_draw_framebuffer != NULL ?
				_opengl_state.bound_draw_framebuffer->gcmSurface.colorOffset[0] :
				color_offset[curr_fb];

  rsxSetTransferScaleMode(context, GCM_TRANSFER_LOCAL_TO_LOCAL, GCM_TRANSFER_SURFACE);
  rsxSetTransferScaleSurface(context, &scale, &surface);
  rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
}

GLenum glCheckFramebufferStatus(GLenum target) {
    struct ps3gl_framebuffer* fb = target == GL_READ_FRAMEBUFFER ? _opengl_state.bound_read_framebuffer : _opengl_state.bound_draw_framebuffer;
    if (!fb) return GL_FRAMEBUFFER_COMPLETE;
    if (!fb->fbTexture || !fb->fbTexture->data || !fb->depthData) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (attachment != GL_COLOR_ATTACHMENT0 || textarget != GL_TEXTURE_2D || level != 0) {
        setError(GL_INVALID_OPERATION); return;
    }
    struct ps3gl_framebuffer* fb = target == GL_READ_FRAMEBUFFER
        ? _opengl_state.bound_read_framebuffer : _opengl_state.bound_draw_framebuffer;
    if (!fb) { setError(GL_INVALID_OPERATION); return; }
    if (texture == 0) { fb->fbTexture = NULL; ++renderTargetRevision; return; }
    if (!ps3glTextureReady(texture)) { setError(GL_INVALID_OPERATION); return; }
    struct ps3gl_texture* tx = &_opengl_state.textures[texture];
    if ((tx->gcmTexture.format & ~GCM_TEXTURE_FORMAT_LIN) != GCM_TEXTURE_FORMAT_A8R8G8B8) {
        setError(GL_INVALID_OPERATION); return;
    }
    uint32_t depthPitch = ps3glPixelPitch(tx->gcmTexture.width, 4);
    uint32_t depthSize = depthPitch * tx->gcmTexture.height;
    /* Allocate first so a failed reattachment leaves the old target usable. */
    if (!fb->depthData || fb->depthSize < depthSize) {
        void* replacement = rsxMemalign(128, depthSize);
        if (!replacement) { setError(GL_OUT_OF_MEMORY); return; }
        memset(replacement, 0, depthSize);
        if (fb->depthData) { glFinish(); rsxFree(fb->depthData); }
        fb->depthData = replacement;
        fb->depthSize = depthSize;
    }
    fb->fbTexture = tx;
    gcmSurface* sf = &fb->gcmSurface;
    sf->colorOffset[0] = tx->gcmTexture.offset;
    sf->colorPitch[0] = tx->gcmTexture.pitch;
    sf->width = tx->gcmTexture.width;
    sf->height = tx->gcmTexture.height;
    sf->depthPitch = depthPitch;
    rsxAddressToOffset(fb->depthData, &sf->depthOffset);
    ++renderTargetRevision;
}

void glGetIntegerv( GLenum pname, GLint *params )
{
	if(pname == GL_FRAMEBUFFER_BINDING) *params = _opengl_state.bound_draw_framebuffer ? _opengl_state.bound_draw_framebuffer->id : 0;
	if(pname == GL_READ_FRAMEBUFFER_BINDING) *params = _opengl_state.bound_read_framebuffer ? _opengl_state.bound_read_framebuffer->id : 0;
    if(pname == GL_TEXTURE_BINDING_2D) *params = _ps3gl_active_bound_tex() ? _ps3gl_active_bound_tex()->id : 0;
    if(pname == GL_PACK_ALIGNMENT) *params = 1;
}

void glPixelStorei( GLenum pname, GLint param ) {}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid* pixels) {
    if (width < 0 || height < 0 || !pixels) { setError(GL_INVALID_VALUE); return; }
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) { setError(GL_INVALID_ENUM); return; }
    struct ps3gl_framebuffer* fb = _opengl_state.bound_read_framebuffer;
    if (fb && (!fb->fbTexture || !fb->fbTexture->data)) { setError(GL_INVALID_OPERATION); return; }
    unsigned w = fb ? fb->gcmSurface.width : display_width;
    unsigned h = fb ? fb->gcmSurface.height : display_height;
    u32 pitch = fb ? fb->gcmSurface.colorPitch[0] : color_pitch;
    u32 offset = fb ? fb->gcmSurface.colorOffset[0] : color_offset[curr_fb];
    if ((size_t)width > SIZE_MAX / 4 / (height ? (size_t)height : 1)) {setError(GL_INVALID_VALUE);return;}
    memset(pixels,0,(size_t)width*height*4);
    int64_t x0=x<0?0:x, y0=y<0?0:y;
    int64_t x1=(int64_t)x+width,y1=(int64_t)y+height;
    if(x1>w)x1=w;if(y1>h)y1=h;
    if(x0>=x1||y0>=y1)return;
    u32 rowBytes=(u32)(x1-x0)*4;
    u32 maxRows=PS3GL_READBACK_CAPACITY/rowBytes;
    if(!maxRows){setError(GL_OUT_OF_MEMORY);return;}
    for(int64_t row=y0;row<y1;) {
        u32 count=(u32)(y1-row);if(count>maxRows)count=maxRows;
        u32 source=offset+(u32)(h-row-count)*pitch+(u32)x0*4;
        const uint8_t* data=rsxReadbackTransfer(source,pitch,rowBytes,count);
        if(!data){setError(GL_OUT_OF_MEMORY);return;}
        for(u32 j=0;j<count;++j) {
            const uint8_t* in=data+(size_t)(count-1-j)*rowBytes;
            uint8_t* out=(uint8_t*)pixels+((size_t)(row-y+j)*width+(size_t)(x0-x))*4;
            for(u32 col=0;col<rowBytes;col+=4) {
                out[col]=in[col+1];out[col+1]=in[col+2];out[col+2]=in[col+3];out[col+3]=in[col];
            }
        }
        row+=count;
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    if (texture >= MAX_TEXTURES) { setError(GL_INVALID_VALUE); return; }
    if (texture && !_opengl_state.textures[texture].allocated) initTexture(texture);
    _opengl_state.textures[texture].target = target;
    _opengl_state.bound_textures[_opengl_state.active_texture_unit] = &_opengl_state.textures[texture];
    _opengl_state.texture_unit_enabled[_opengl_state.active_texture_unit] = texture != 0;
}

void glActiveTexture(GLenum texture)
{
	GLuint unit = (GLuint)(texture - GL_TEXTURE0);
	if (unit >= MAX_TEX_UNITS) return;
	_opengl_state.active_texture_unit = unit;
}

/*
 * Fog
 */

void glFogi( GLenum pname, GLint param )
{
	switch(pname)
	{
		case GL_FOG_MODE:
			_opengl_state.fog_mode = param;
			break;
		case GL_FOG_START:
		case GL_FOG_END:
		case GL_FOG_DENSITY:
			glFogf(pname, (GLfloat)param);
			break;
		default:
			break;
	}
}

void glFogf( GLenum pname, GLfloat param )
{

	switch(pname)
	{
		case GL_FOG_MODE:
			glFogi(pname, (GLint)param);
			break;
		case GL_FOG_START:
			_opengl_state.fog_start = param;
			break;
		case GL_FOG_END:
			_opengl_state.fog_end = param;
			break;
		case GL_FOG_DENSITY:
			_opengl_state.fog_density = param;
			break;
		default:
			break;
	}
}

void glFogfv( GLenum pname, const GLfloat *params )
{
	if(pname == GL_FOG_COLOR)
	{
		if(params != NULL)
			memcpy(_opengl_state.fog_color, params, 4*sizeof(GLfloat));
	}
}

void glBlendEquation( GLenum mode )
{
	_opengl_state.blend_equation = mode;
}

void glBlendColor( GLclampf red, GLclampf green,
                                    GLclampf blue, GLclampf alpha )
{
	// Used in rsxSetBlendColor
	_opengl_state.blend_color_rsx =
	 (((uint8_t)(alpha * 255.0f)) << 24) |
    (((uint8_t)(red   * 255.0f)) << 16) |
    (((uint8_t)(green * 255.0f)) << 8)  |
    (((uint8_t)(blue  * 255.0f)) << 0);
}


/*
 * Shaders / Programs
 */

static struct ps3gl_shader* lookupShader(GLuint id)
{
	if (id == 0 || id >= MAX_SHADERS) return NULL;
	if (!_opengl_state.shaders[id].allocated) return NULL;
	return &_opengl_state.shaders[id];
}

static struct ps3gl_program* lookupProgram(GLuint id)
{
	if (id == 0 || id >= MAX_PROGRAMS) return NULL;
	if (!_opengl_state.programs[id].allocated) return NULL;
	return &_opengl_state.programs[id];
}

GLuint glCreateShader(GLenum type)
{
	if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) return 0;
	for (GLuint i = 1; i < MAX_SHADERS; i++) {
		if (!_opengl_state.shaders[i].allocated) {
			struct ps3gl_shader *s = &_opengl_state.shaders[i];
			s->allocated = true;
			s->type = type;
			s->blob = NULL;
			s->blobSize = 0;
			s->fpUcode = NULL;
			s->fpOffset = 0;
			return i;
		}
	}
	return 0;
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length)
{
	if (count != 1 || shaders == NULL || binary == NULL || length <= 0) return;
	struct ps3gl_shader *s = lookupShader(shaders[0]);
	if (s == NULL) return;

	if (binaryFormat == PS3GL_SHADER_BINARY_VPO && s->type != GL_VERTEX_SHADER) return;
	if (binaryFormat == PS3GL_SHADER_BINARY_FPO && s->type != GL_FRAGMENT_SHADER) return;

	if (s->blob != NULL) free(s->blob);
	s->blob = malloc(length);
	memcpy(s->blob, binary, length);
	s->blobSize = length;

	if (s->type == GL_FRAGMENT_SHADER) {
		// FP ucode must live in RSX-visible memory.
		rsxFragmentProgram *prog = (rsxFragmentProgram*)s->blob;
		void *ucode = NULL;
		u32 ucodeSize = 0;
		rsxFragmentProgramGetUCode(prog, &ucode, &ucodeSize);

		if (s->fpUcode != NULL) rsxFree(s->fpUcode);
		s->fpUcode = rsxMemalign(64, ucodeSize);
		memcpy(s->fpUcode, ucode, ucodeSize);
		rsxAddressToOffset(s->fpUcode, &s->fpOffset);
	}
}

void glDeleteShader(GLuint shader)
{
	struct ps3gl_shader *s = lookupShader(shader);
	if (s == NULL) return;
	if (s->blob != NULL) { free(s->blob); s->blob = NULL; }
	if (s->fpUcode != NULL) { rsxFree(s->fpUcode); s->fpUcode = NULL; }
	s->allocated = false;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	if (params == NULL) return;
	struct ps3gl_shader *s = lookupShader(shader);
	if (pname == GL_COMPILE_STATUS) {
		*params = (s != NULL && s->blob != NULL) ? GL_TRUE : GL_FALSE;
	} else if (pname == GL_INFO_LOG_LENGTH) {
		*params = 0;
	}
}

GLuint glCreateProgram(void)
{
	for (GLuint i = 1; MAX_PROGRAMS > i; i++) {
		if (!_opengl_state.programs[i].allocated) {
			struct ps3gl_program *p = &_opengl_state.programs[i];
			p->allocated = true;
			p->linked = false;
			p->vertexShader = 0;
			p->fragmentShader = 0;
			p->uniformCount = 0;
	memset(p->uniforms,0,sizeof(p->uniforms));
			return i;
		}
	}
	return 0;
}

void glAttachShader(GLuint program, GLuint shader)
{
	struct ps3gl_program *p = lookupProgram(program);
	struct ps3gl_shader *s = lookupShader(shader);
	if (p == NULL || s == NULL) return;
	if (s->type == GL_VERTEX_SHADER) p->vertexShader = shader;
	else if (s->type == GL_FRAGMENT_SHADER) p->fragmentShader = shader;
	p->linked = false;
}

void glDetachShader(GLuint program, GLuint shader)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;
	if (p->vertexShader == shader) p->vertexShader = 0;
	if (p->fragmentShader == shader) p->fragmentShader = 0;
	p->linked = false;
}

// cgcomp's eparams enum (PSL1GHT/tools/cgcomp/include/parser.h) packs sampler types into the range PARAM_SAMPLER1D..PARAM_SAMPLERSHADOWRECT == 14..21.
// These values are baked into every compiled .fpo and effectively stable.
#define PS3GL_PARAM_SAMPLER_FIRST 14
#define PS3GL_PARAM_SAMPLER_LAST  21

void glLinkProgram(GLuint program)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;

	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	struct ps3gl_shader *fs = lookupShader(p->fragmentShader);
	// At least one stage must be attached.
	// The unattached stage falls back to the FFP shader at draw time.
	if ((vs == NULL || vs->blob == NULL) && (fs == NULL || fs->blob == NULL)) {
		p->linked = false;
		return;
	}

	p->uniformCount = 0;
	memset(p->uniforms,0,sizeof(p->uniforms));

	// The OpenGL spec says that samplers default to texture unit 0 at program creation.
	// To match this behavior, we'll prepopulate all sampler attribs to 0 to avoid unforeseen consequences.
	if (fs != NULL && fs->blob != NULL) {
		rsxFragmentProgram *fp = (rsxFragmentProgram*) fs->blob;
		u16 numAttr = rsxFragmentProgramGetNumAttrib(fp);
		rsxProgramAttrib *attribs = rsxFragmentProgramGetAttribs(fp);
		for (u16 i = 0; numAttr > i; i++) {
			if (p->uniformCount >= MAX_PROGRAM_UNIFORMS)
				break;
			extern bool gPS3GL_DebugLogEnabled;
			if (gPS3GL_DebugLogEnabled && attribs[i].name_off) {
				const char *name = ((const char*) fp) + attribs[i].name_off;
				PS3SessionLog_event("glLinkProgram param: name=%s type=%d index=%d", name, attribs[i].type, attribs[i].index);
			}

			if (!attribs[i].name_off)
				continue;
			if (PS3GL_PARAM_SAMPLER_FIRST > attribs[i].type)
				continue;
			if (attribs[i].type > PS3GL_PARAM_SAMPLER_LAST)
				continue;

			const char *name = ((const char*) fp) + attribs[i].name_off;
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_FP;
			u->constHandle = NULL;
			u->samplerUnit = 0;
			u->samplerAttrib = &attribs[i];
			p->uniformCount++;
			extern bool gPS3GL_DebugLogEnabled;
			if (gPS3GL_DebugLogEnabled) {
				PS3SessionLog_event("glLinkProgram: SAMPLER name=%s type=%d index=%d loc=%d", name, attribs[i].type, attribs[i].index, p->uniformCount - 1);
			}
		}
	}

	p->linked = true;
}

void glUseProgram(GLuint program)
{
	if (program == 0) { _opengl_state.active_program = 0; return; }
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked) return;
	_opengl_state.active_program = program;
}

void glDeleteProgram(GLuint program)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL) return;
	if (_opengl_state.active_program == program) _opengl_state.active_program = 0;
	p->allocated = false;
	p->linked = false;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
	if (params == NULL) return;
	struct ps3gl_program *p = lookupProgram(program);
	if (pname == GL_LINK_STATUS) {
		*params = (p != NULL && p->linked) ? GL_TRUE : GL_FALSE;
	} else if (pname == GL_INFO_LOG_LENGTH) {
		*params = 0;
	}
}

// Find or insert a uniform slot for a given name on a linked program.
static GLint resolveUniform(struct ps3gl_program *p, const GLchar *name)
{
	for (GLuint i = 0; p->uniformCount > i; i++) {
		if (strcmp(p->uniforms[i].name, name) == 0) {
			return PS3GL_LOC_PACK(p->uniforms[i].stage, i);
		}
	}
	if (p->uniformCount >= MAX_PROGRAM_UNIFORMS) return -1;

	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	struct ps3gl_shader *fs = lookupShader(p->fragmentShader);

	// Samplers are pre-populated by glLinkProgram, so they'll always hit the cache above.
	// Only non-sampler FP consts need lazy resolution here.
	if (fs != NULL && fs->blob != NULL) {
		rsxFragmentProgram *fp = (rsxFragmentProgram*)fs->blob;
		rsxProgramConst *c = rsxFragmentProgramGetConst(fp, (char*)name);
		if (c != NULL) {
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_FP;
			u->constHandle = c;
			u->samplerUnit = -1;
			u->samplerAttrib = NULL;
			return PS3GL_LOC_PACK(PS3GL_LOC_STAGE_FP, p->uniformCount++);
		}
	}

	if (vs != NULL && vs->blob != NULL) {
		rsxVertexProgram *vp = (rsxVertexProgram*)vs->blob;
		rsxProgramConst *c = rsxVertexProgramGetConst(vp, (char*)name);
		if (c != NULL) {
			struct ps3gl_program_uniform *u = &p->uniforms[p->uniformCount];
			strncpy(u->name, name, sizeof(u->name) - 1);
			u->name[sizeof(u->name) - 1] = '\0';
			u->stage = PS3GL_LOC_STAGE_VP;
			u->constHandle = c;
			u->samplerUnit = -1;
			u->samplerAttrib = NULL;
			return PS3GL_LOC_PACK(PS3GL_LOC_STAGE_VP, p->uniformCount++);
		}
	}

	return -1;
}

GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked || name == NULL) return -1;
	return resolveUniform(p, name);
}

GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
	struct ps3gl_program *p = lookupProgram(program);
	if (p == NULL || !p->linked || name == NULL) return -1;
	struct ps3gl_shader *vs = lookupShader(p->vertexShader);
	if (vs == NULL || vs->blob == NULL) return -1;
	rsxProgramAttrib *a = rsxVertexProgramGetAttrib((rsxVertexProgram*)vs->blob, (char*)name);
	if (a == NULL) return -1;
	return a->index;
}

static struct ps3gl_program_uniform* uniformAt(GLint location)
{
	if (location < 0) return NULL;
	struct ps3gl_program *p = lookupProgram(_opengl_state.active_program);
	if (p == NULL) return NULL;
	GLuint idx = PS3GL_LOC_INDEX(location);
	if (idx >= p->uniformCount) return NULL;
	return &p->uniforms[idx];
}

static struct ps3gl_shader* activeShaderForStage(GLubyte stage)
{
	struct ps3gl_program *p = lookupProgram(_opengl_state.active_program);
	if (p == NULL) return NULL;
	return lookupShader(stage == PS3GL_LOC_STAGE_VP ? p->vertexShader : p->fragmentShader);
}

void glUniform1i(GLint location, GLint v0)
{
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL) return;
	// TODO: Implement sampler attributes!
	if (u->samplerAttrib != NULL) {
		u->samplerUnit = v0;
		return;
	}
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32(context, (rsxFragmentProgram*)s->blob, u->constHandle, (float)v0, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		float v = (float)v0;
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, &v);
	}
}

void glUniform1f(GLint location, GLfloat v0)
{
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL) return;
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32(context, (rsxFragmentProgram*)s->blob, u->constHandle, v0, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		float v[4] = {v0, 0, 0, 0};
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, v);
	}
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
	if (count != 1 || value == NULL) return;
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL) return;
	struct ps3gl_shader *s = activeShaderForStage(u->stage);
	if (s == NULL || s->blob == NULL) return;
	if (u->stage == PS3GL_LOC_STAGE_FP) {
		rsxSetFragmentProgramParameterF32Vec4(context, (rsxFragmentProgram*)s->blob, u->constHandle, (float*)value, s->fpOffset, GCM_LOCATION_RSX);
	} else {
		rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, (float*)value);
	}
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	GLfloat v[4] = {v0, v1, 0, 0};
	glUniform4fv(location, 1, v);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
	GLfloat v[4] = {v0, v1, v2, 0};
	glUniform4fv(location, 1, v);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	GLfloat v[4] = {v0, v1, v2, v3};
	glUniform4fv(location, 1, v);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
	if (count != 1 || value == NULL) return;
	GLfloat v[4] = {value[0], value[1], value[2], 0};
	glUniform4fv(location, 1, v);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	if (count != 1 || value == NULL || transpose) return;
	struct ps3gl_program_uniform *u = uniformAt(location);
	if (u == NULL || u->constHandle == NULL || u->stage != PS3GL_LOC_STAGE_VP) return;
	struct ps3gl_shader *s = activeShaderForStage(PS3GL_LOC_STAGE_VP);
	if (s == NULL || s->blob == NULL) return;
	rsxSetVertexProgramParameter(context, (rsxVertexProgram*)s->blob, u->constHandle, (float*)value);
}

/* PS3GL Functions */

static void _program_exit_callback(void)
{
	if (!context) return;
	gcmSetWaitFlip(context);
	rsxFinish(context,1);
}

// Bind a GL texture unit's currently-bound texture into the given RSX slot.
static void _ps3gl_bind_unit_to_rsx_slot(GLuint glUnit, int rsxSlot)
{
	struct ps3gl_texture *tex = _opengl_state.bound_textures[glUnit];
	gcmTexture* gcmTex = &_opengl_dummy_texture;

	if (tex != NULL && tex->gcmTexture.width != 0 && tex->gcmTexture.height != 0 && tex->data != NULL) {
		gcmTex = &tex->gcmTexture;
	}

	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("RSX_TEX_BIND: unit=%u slot=%d texPtr=%p gcmOffset=%08X w=%d h=%d pitch=%d",
			glUnit, rsxSlot, (void*)tex, gcmTex->offset, gcmTex->width, gcmTex->height, gcmTex->pitch);
	}

	rsxLoadTexture(context, rsxSlot, gcmTex);

	rsxTextureControl(context,
		rsxSlot,
		GCM_TRUE,
		0<<8,  // TODO: minLOD
		12<<8, // TODO: maxLOD
		GCM_TEXTURE_MAX_ANISO_1);

	if (gcmTex != &_opengl_dummy_texture) {
		rsxTextureFilter(context,
			rsxSlot,
			0, // TODO: bias
			tex->minFilter,
			tex->magFilter,
			GCM_TEXTURE_CONVOLUTION_QUINCUNX);
		rsxTextureWrapMode(context,
			rsxSlot,
			tex->wrapS,
			tex->wrapT,
			GCM_TEXTURE_CLAMP_TO_EDGE, // TODO: wrapR
			0, // TODO: unsignedRemap
			GCM_TEXTURE_ZFUNC_LESS, // TODO: zfunc
			0); // TODO: gamma
	} else {
		rsxTextureFilter(context,
			rsxSlot,
			0, 
			GCM_TEXTURE_NEAREST,
			GCM_TEXTURE_NEAREST,
			GCM_TEXTURE_CONVOLUTION_QUINCUNX);
		rsxTextureWrapMode(context,
			rsxSlot,
			GCM_TEXTURE_CLAMP_TO_EDGE,
			GCM_TEXTURE_CLAMP_TO_EDGE,
			GCM_TEXTURE_CLAMP_TO_EDGE,
			0,
			GCM_TEXTURE_ZFUNC_LESS,
			0);
	}
}

void _ps3gl_load_texture(void)
{
	static GLuint last_bound_tex0 = 0xFFFFFFFF;
	struct ps3gl_texture *tex0 = _opengl_state.bound_textures[0];
	GLuint tex0_id = tex0 ? tex0->id : 0;
	if (tex0_id != last_bound_tex0) {
		rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
		last_bound_tex0 = tex0_id;
	}

	if (_opengl_state.active_program == 0) {
		// FFP path: single sampler, hardwired to GL unit 0.
		if (_opengl_state.ffp_tex_unit == NULL) return;
		_ps3gl_bind_unit_to_rsx_slot(0, _opengl_state.ffp_tex_unit->index);
		return;
	}

	// Custom program: walk its cached sampler uniforms.
	// samplerUnit (set by glUniform1i) is the GL-side unit index;
	// samplerAttrib->index is the RSX slot pinned by Cg's : TEXUNITn semantic.
	struct ps3gl_program *p = &_opengl_state.programs[_opengl_state.active_program];
	for (GLuint i = 0; p->uniformCount > i; i++) {
		struct ps3gl_program_uniform *u = &p->uniforms[i];
		if (u->samplerAttrib == NULL) continue;
		GLuint glUnit = (u->samplerUnit >= 0) ? (GLuint) u->samplerUnit : 0;
		if (glUnit >= MAX_TEX_UNITS) continue;
		_ps3gl_bind_unit_to_rsx_slot(glUnit, u->samplerAttrib->index);
	}
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	if (params == NULL) return;
	if (pname == GL_MODELVIEW_MATRIX) {
		__builtin_memcpy(params, (float*)&_opengl_state.modelview_matrix, sizeof(float)*16);
	} else if (pname == GL_PROJECTION_MATRIX) {
		__builtin_memcpy(params, (float*)&_opengl_state.projection_matrix, sizeof(float)*16);
	}
}

bool gPS3GL_DebugLogEnabled = false;

static void _ps3gl_setup_render_target(void) {
    static struct ps3gl_framebuffer* last_bound_fb = (struct ps3gl_framebuffer*)0xFFFFFFFF;
    static u32 last_bound_curr_fb = 0xFFFFFFFF;
    static uint32_t lastRevision = UINT32_MAX;
    if (_opengl_state.bound_draw_framebuffer != NULL) {
        if (last_bound_fb != _opengl_state.bound_draw_framebuffer || lastRevision != renderTargetRevision) {
            rsxSetSurface(context, &_opengl_state.bound_draw_framebuffer->gcmSurface);
            lastRevision = renderTargetRevision;
            last_bound_fb = _opengl_state.bound_draw_framebuffer;
            last_bound_curr_fb = 0xFFFFFFFF;
        }
    } else {
        if (last_bound_curr_fb != curr_fb || last_bound_fb != NULL) {
            setRenderTarget(curr_fb);
            last_bound_curr_fb = curr_fb;
            last_bound_fb = NULL;
        }
    }
}

static void _ps3gl_setup_scissor(void) {
    unsigned w = _opengl_state.bound_draw_framebuffer ? _opengl_state.bound_draw_framebuffer->gcmSurface.width : display_width;
    unsigned h = _opengl_state.bound_draw_framebuffer ? _opengl_state.bound_draw_framebuffer->gcmSurface.height : display_height;
    Ps3glRasterRect r = {0, 0, (uint16_t)w, (uint16_t)h};
    if (_opengl_state.scissor.enabled) {
        r = ps3glClipRasterRect(_opengl_state.scissor.x, _opengl_state.scissor.y,
                                _opengl_state.scissor.w, _opengl_state.scissor.h, w, h);
    }
    rsxSetScissor(context, r.x, r.y, r.w, r.h);
    lastSubmittedScissor = r;
}

void _setup_draw_env(void) {
    extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
        PS3SessionLog_event("DRAW_ENV START");
    }
    _ps3gl_setup_render_target();

	rsxSetShadeModel(context, _opengl_state.shade_model);
	rsxSetPointSize(context, _opengl_state.point_size);

	rsxSetColorMask(context, _opengl_state.color_mask);
	rsxSetColorMaskMrt(context,0);

	// Alpha
	rsxSetAlphaTestEnable(context, _opengl_state.alpha_test_enabled);
	rsxSetAlphaFunc(context, _opengl_state.alpha_func, _opengl_state.alpha_func_ref);

	// Blend
	// TODO: BlendFuncSeparate/BlendEquationSeparate
	rsxSetBlendEnable(context, _opengl_state.blend_enabled);
	rsxSetBlendColor(context, _opengl_state.blend_color_rsx, 0);
	rsxSetBlendFunc(context, 
		_opengl_state.blend_func_sfactor, 
		_opengl_state.blend_func_dfactor, 
		_opengl_state.blend_func_sfactor_alpha, 
		_opengl_state.blend_func_dfactor_alpha
	);
	rsxSetBlendEquation(context, _opengl_state.blend_equation, _opengl_state.blend_equation);
	
	rsxSetCullFaceEnable(context, _opengl_state.cull_face_enabled);
	rsxSetCullFace(context, _opengl_state.cull_face);

	rsxSetLogicOpEnable(context, _opengl_state.logic_op_enabled);
	rsxSetLogicOp(context, _opengl_state.logic_op);

	// Depth Testing
	rsxSetDepthTestEnable(context, _opengl_state.depth_test);
	rsxSetDepthWriteEnable(context, _opengl_state.depth_mask);
	rsxSetDepthFunc(context, _opengl_state.depth_func);

	switch(_opengl_state.polygon_mode_face)
	{
		case GL_FRONT_AND_BACK:
			rsxSetFrontPolygonMode(context, _opengl_state.polygon_mode);
			rsxSetBackPolygonMode(context, _opengl_state.polygon_mode);
			break;
		case GL_FRONT:
			rsxSetFrontPolygonMode(context, _opengl_state.polygon_mode);
			break;
		case GL_BACK:
			rsxSetBackPolygonMode(context, _opengl_state.polygon_mode);
			break;
		default:
			break;
	}

	// Fog
	if(_opengl_state.fog_enabled)
	{
		rsxSetFogMode(context, _opengl_state.fog_mode);
		float p0 = 0;
		float p1 = 0;
		switch(_opengl_state.fog_mode)
		{
			case GCM_FOG_MODE_LINEAR:
				// TODO: Confirm these are right
				p0 = _opengl_state.fog_end/(_opengl_state.fog_end-_opengl_state.fog_start);
				p1 = 1/(_opengl_state.fog_end-_opengl_state.fog_start);
				break;
			// TODO: Figure out EXP and EXP2 params
			default:
				break;

		}
		rsxSetFogParams(context, p0, p1);
	}

	// Clip unsigned hardware bounds without changing the logical transform.
	unsigned targetW = _opengl_state.bound_draw_framebuffer ? _opengl_state.bound_draw_framebuffer->gcmSurface.width : display_width;
	unsigned targetH = _opengl_state.bound_draw_framebuffer ? _opengl_state.bound_draw_framebuffer->gcmSurface.height : display_height;
	Ps3glRasterRect viewport = ps3glClipRasterRect(_opengl_state.viewport.x, _opengl_state.viewport.y,
		_opengl_state.viewport.w, _opengl_state.viewport.h, targetW, targetH);
	lastSubmittedViewport = viewport;
	ps3glViewportTransform(_opengl_state.viewport.x, _opengl_state.viewport.y,
		_opengl_state.viewport.w, _opengl_state.viewport.h, targetH,
		_opengl_state.depth_near, _opengl_state.depth_far,
		_opengl_state.viewport.scale, _opengl_state.viewport.offset);
	rsxSetViewport(context,
		viewport.x, viewport.y, viewport.w, viewport.h,
		_opengl_state.depth_near,
		_opengl_state.depth_far, 
		_opengl_state.viewport.scale, 
		_opengl_state.viewport.offset
	);

	_ps3gl_setup_scissor();

	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("DRAW_ENV: state commands done, loading texture...");
	}

	// Load Current Texture
	_ps3gl_load_texture();

	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("DRAW_ENV: texture loaded, loading shaders...");
	}

	// Resolve which programs to actually run this draw.
	// If the user has glUseProgram'd a custom program, then use the attached shader for each stage, otherwise fall back to the FFP for that stage.
	rsxVertexProgram *useVpo = vpo;
	void *useVpUcode = vp_ucode;
	rsxFragmentProgram *useFpo = fpo;
	u32 useFpOffset = fp_offset;
	bool ffpFp = true;
	bool ffpVp = true;

	if (_opengl_state.active_program != 0) {
		struct ps3gl_program *p = &_opengl_state.programs[_opengl_state.active_program];
		if (p->allocated && p->linked) {
			if (p->vertexShader != 0) {
				struct ps3gl_shader *vs = &_opengl_state.shaders[p->vertexShader];
				if (vs->allocated && vs->blob != NULL) {
					useVpo = (rsxVertexProgram*)vs->blob;
					u32 sz = 0;
					rsxVertexProgramGetUCode(useVpo, &useVpUcode, &sz);
					ffpVp = false;
				}
			}
			if (p->fragmentShader != 0) {
				struct ps3gl_shader *fs = &_opengl_state.shaders[p->fragmentShader];
				if (fs->allocated && fs->blob != NULL && fs->fpUcode != NULL) {
					useFpo      = (rsxFragmentProgram*)fs->blob;
					useFpOffset = fs->fpOffset;
					ffpFp       = false;
				}
			}
		}
	}

	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("DRAW_ENV: loading VP and FP. fp_offset=%08X ffpVp=%d ffpFp=%d active_program=%d", useFpOffset, ffpVp, ffpFp, _opengl_state.active_program);
	}

	rsxLoadVertexProgram(context, useVpo, useVpUcode);
	rsxLoadFragmentProgramLocation(context, useFpo, useFpOffset, GCM_LOCATION_RSX);

	if (ffpVp) {
		// FFP vertex program: push the matrix uniforms it expects.

		rsxSetVertexProgramParameter(context, useVpo, _opengl_state.prog_consts[PS3GL_Uniform_ModelViewMatrix],  (float*)&_opengl_state.modelview_matrix);
		rsxSetVertexProgramParameter(context, useVpo, _opengl_state.prog_consts[PS3GL_Uniform_ProjectionMatrix], (float*)&_opengl_state.projection_matrix);
	}

	if (ffpFp) {
		bool hardware_tex_enabled = false;
		struct ps3gl_texture *tex = _opengl_state.bound_textures[0];
		if (_opengl_state.texture_unit_enabled[0] && tex != NULL && tex->id != 0 && tex->gcmTexture.width != 0 && tex->gcmTexture.height != 0 && tex->data != NULL) {
			hardware_tex_enabled = true;
		}
		
		extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
			PS3SessionLog_event("FFP FP CONFIG: texEnabled=%d texUnit0Enabled=%d texPtr=%p offset=%08X w=%d h=%d",
				hardware_tex_enabled, _opengl_state.texture_unit_enabled[0], (void*)tex,
				tex ? tex->gcmTexture.offset : 0, tex ? tex->gcmTexture.width : 0, tex ? tex->gcmTexture.height : 0);
		}
		
		// FFP FP: push its baked uniforms.
		rsxSetFragmentProgramParameterBool(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_TextureEnabled], hardware_tex_enabled, useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterBool(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_FogEnabled], _opengl_state.fog_enabled, useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterF32(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_TextureMode], _opengl_state.texEnvMode, useFpOffset, GCM_LOCATION_RSX);
		rsxSetFragmentProgramParameterF32Vec4(context, useFpo, _opengl_state.prog_consts[PS3GL_Uniform_FogColor],  _opengl_state.fog_color, useFpOffset, GCM_LOCATION_RSX);
	}

	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("DRAW_ENV END");
	}
}

// TODO: This is a placeholder, replace with good api, closer to vitaGL
// Also move rsxutil.c functionality over to here
void ps3glInit(void)
{
	atexit(_program_exit_callback);
	void *host_addr = memalign(1024*1024,HOST_SIZE);
	init_screen(host_addr,HOST_SIZE);

	u32 vpsize = 0;
	rsxVertexProgramGetUCode(vpo, &vp_ucode, &vpsize);
	_opengl_state.prog_consts[PS3GL_Uniform_ModelViewMatrix] = rsxVertexProgramGetConst(vpo, "uModelViewMatrix");
	_opengl_state.prog_consts[PS3GL_Uniform_ProjectionMatrix] = rsxVertexProgramGetConst(vpo, "uProjectionMatrix");
	
	rsxProgramAttrib *aPos = rsxVertexProgramGetAttrib(vpo, "aPos");
	rsxProgramAttrib *aColor = rsxVertexProgramGetAttrib(vpo, "aColor");
	rsxProgramAttrib *aTexCoord0 = rsxVertexProgramGetAttrib(vpo, "aTexCoord0");
	if (aPos) g_vpo_aPos = aPos->index;
	if (aColor) g_vpo_aColor = aColor->index;
	if (aTexCoord0) g_vpo_aTexCoord0 = aTexCoord0->index;
	
	extern bool gPS3GL_DebugLogEnabled; if (gPS3GL_DebugLogEnabled) {
		PS3SessionLog_event("FFP_SHADER_VPO Attribs: aPos=%d aColor=%d aTexCoord0=%d", 
			aPos ? aPos->index : -1,
			aColor ? aColor->index : -1,
			aTexCoord0 ? aTexCoord0->index : -1);
	}



	u32 fpsize = 0;
	rsxFragmentProgramGetUCode(fpo, &fp_ucode, &fpsize);
	_opengl_state.ffp_tex_unit = rsxFragmentProgramGetAttrib(fpo, "uTextureUnit0");
	_opengl_state.prog_consts[PS3GL_Uniform_TextureEnabled] = rsxFragmentProgramGetConst(fpo, "uTextureEnabled");
	_opengl_state.prog_consts[PS3GL_Uniform_TextureMode] = rsxFragmentProgramGetConst(fpo, "uTextureMode");

	_opengl_state.prog_consts[PS3GL_Uniform_FogEnabled] = rsxFragmentProgramGetConst(fpo, "uFogEnabled");
	_opengl_state.prog_consts[PS3GL_Uniform_FogColor] = rsxFragmentProgramGetConst(fpo, "uFogColor");

	// Setup dummy texture
	void* dummy_tex_data = rsxMemalign(128, 64);
	*(uint32_t*)dummy_tex_data = 0xFFFFFFFF; // white pixel
	uint32_t dummy_tex_offset;
	rsxAddressToOffset(dummy_tex_data, &dummy_tex_offset);
	_opengl_dummy_texture.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	_opengl_dummy_texture.mipmap = 1;
	_opengl_dummy_texture.dimension = GCM_TEXTURE_DIMS_2D;
	_opengl_dummy_texture.cubemap = GCM_FALSE;
	_opengl_dummy_texture.remap = GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT | GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT | GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT | GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT | GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT | GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT | GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT | GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT;
	_opengl_dummy_texture.width = 1;
	_opengl_dummy_texture.height = 1;
	_opengl_dummy_texture.depth = 1;
	_opengl_dummy_texture.pitch = 64;
	_opengl_dummy_texture.location = GCM_LOCATION_RSX;
	_opengl_dummy_texture.offset = dummy_tex_offset;

	fp_buffer = (u32*)rsxMemalign(64,fpsize);
	memcpy(fp_buffer,fp_ucode,fpsize);
	rsxAddressToOffset(fp_buffer,&fp_offset);

	// Set default state
	glColor3f(1.0f, 1.0f, 1.0f);
	glColorMask(true, true, true, true);

	glPointSize(1.0f);
	glShadeModel(GL_SMOOTH);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glFrontFace(GL_CCW);
	
	glDisable(GL_ALPHA_TEST); 
	glAlphaFunc(GL_ALWAYS, 0);

	glDisable(GL_BLEND); 
	glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ZERO);

	glDisable(GL_CULL_FACE); 
	glCullFace(GL_BACK);

	glDisable(GL_COLOR_LOGIC_OP); 
	glLogicOp(GL_COPY);

	glDisable(GL_DEPTH_TEST); 
	glDepthFunc(GL_LESS);
	glDepthMask(true);
	glDepthRange(0, 1);

	glDisable(GL_FOG); 
	glDisable(GL_TEXTURE_2D);

	// Clear Values
	glClearColor(0, 0, 0, 0);
	glClearDepth(1.0f);
	glClearStencil(0);
	glViewport(0, 0, display_width, display_height);
	glScissor(0, 0, display_width, display_height);

	// Matrices
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	_opengl_state.cur_proj_mat = 0;
    _opengl_state.cur_modv_mat = 0;


	// Textures
	_opengl_state.nextTextureID = 1;
	_opengl_state.nextFramebufferID = 1;
	_opengl_state.active_texture_unit = 0;
	for (int u = 0; MAX_TEX_UNITS > u; u++) {
		_opengl_state.bound_textures[u] = &_opengl_state.textures[0];
		_opengl_state.texture_unit_enabled[u] = false;
	}
	_ps3gl_active_bound_tex()->id = 0;
	_ps3gl_active_bound_tex()->allocated = true;
	_ps3gl_active_bound_tex()->data = NULL;
	_ps3gl_active_bound_tex()->minFilter = GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
	_ps3gl_active_bound_tex()->magFilter = GCM_TEXTURE_LINEAR;
	_ps3gl_active_bound_tex()->wrapS = GCM_TEXTURE_REPEAT;
	_ps3gl_active_bound_tex()->wrapT = GCM_TEXTURE_REPEAT;
	_ps3gl_active_bound_tex()->wrapR = GCM_TEXTURE_REPEAT;
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);


}

void ps3glSwapBuffers(void)
{
	PS3SessionLog_trace("RSX DIAG: ps3glSwapBuffers. buffer space used: %d / %d bytes.", (int)((uint8_t*)context->current - (uint8_t*)context->begin), (int)(context->end - context->begin) * 4);
	flip();
}
