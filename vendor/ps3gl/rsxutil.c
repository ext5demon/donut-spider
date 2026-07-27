#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>

#if __has_include(<sysutil/video_out.h>)
#include <sysutil/video_out.h>
#else
// Compatibility for non PS3Aqua PSL1GHT
#include <sysutil/video.h>
#define videoOutState videoState
#define videoOutResolution videoResolution
#define videoOutConfiguration videoConfiguration

#define videoOutGetState videoGetState
#define videoOutGetResolution videoGetResolution
#define videoOutConfigure videoConfigure

#define VIDEO_OUT_BUFFER_FORMAT_XRGB VIDEO_BUFFER_FORMAT_XRGB
#endif

#include "rsxutil.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void PS3SessionLog_event(const char* format, ...);
extern void PS3SessionLog_flushToDisk(void);
#ifdef __cplusplus
}
#endif

#define GCM_LABEL_INDEX		255

videoOutResolution res;
gcmContextData *context = NULL;
u32 *initialCommandBuffer;
u32 commandBufferSize;

u32 curr_fb = 0;
u32 first_fb = 1;

u32 display_width;
u32 display_height;

u32 depth_pitch;
u32 depth_offset;
u32 *depth_buffer;

u32 color_pitch;
u32 color_offset[2];
u32 *color_buffer[2];

static u32 sLabelVal = 1;

void waitFinish(void)
{
	rsxSetWriteBackendLabel(context,GCM_LABEL_INDEX,sLabelVal);

	rsxFlushBuffer(context);

	while(*(vu32*)gcmGetLabelAddress(GCM_LABEL_INDEX)!=sLabelVal)
		usleep(30);

	++sLabelVal;
}

void waitRSXIdle()
{
	rsxSetWriteBackendLabel(context,GCM_LABEL_INDEX,sLabelVal);
	rsxSetWaitLabel(context,GCM_LABEL_INDEX,sLabelVal);

	++sLabelVal;

	waitFinish();
}

void setRenderTarget(u32 index)
{
	gcmSurface sf;

	sf.colorFormat		= GCM_SURFACE_A8R8G8B8;
	sf.colorTarget		= GCM_SURFACE_TARGET_0;
	sf.colorLocation[0]	= GCM_LOCATION_RSX;
	sf.colorOffset[0]	= color_offset[index];
	sf.colorPitch[0]	= color_pitch;

	sf.colorLocation[1]	= GCM_LOCATION_RSX;
	sf.colorLocation[2]	= GCM_LOCATION_RSX;
	sf.colorLocation[3]	= GCM_LOCATION_RSX;
	sf.colorOffset[1]	= 0;
	sf.colorOffset[2]	= 0;
	sf.colorOffset[3]	= 0;
	sf.colorPitch[1]	= 64;
	sf.colorPitch[2]	= 64;
	sf.colorPitch[3]	= 64;

	sf.depthFormat		= GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation	= GCM_LOCATION_RSX;
	sf.depthOffset		= depth_offset;
	sf.depthPitch		= depth_pitch;

	sf.type				= GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias		= GCM_SURFACE_CENTER_1;

	sf.width			= display_width;
	sf.height			= display_height;
	sf.x				= 0;
	sf.y				= 0;

	PS3SessionLog_event("RSX DIAG: setRenderTarget. fb=%d, color_offs=%08X, depth_offs=%08X, color_pitch=%d, depth_pitch=%d", index, sf.colorOffset[0], sf.depthOffset, sf.colorPitch[0], sf.depthPitch);
	rsxSetSurface(context,&sf);
}

void waitflip()
{
	u32 timeout = 0;
	while(gcmGetFlipStatus()!=0) {
		usleep(200);
		timeout++;
		if (timeout > 15000) { // 3 seconds
			PS3SessionLog_event("RSX_PANIC: waitflip timeout! GPU never executed the flip. RSX likely crashed!");
			PS3SessionLog_flushToDisk();
			exit(1);
		}
	}
	gcmResetFlipStatus();
}

// Fixes hangs with GCC15
// Thanks to kd-11 for telling me how to fix the issue!
static void resetCommandBuffer()
{
	PS3SessionLog_event("RSX DIAG: resetCommandBuffer start");
	rsxFinish(context, 1);
	u32 startoffs;
	rsxAddressToOffset(initialCommandBuffer, &startoffs);
	rsxSetJumpCommand(context, startoffs);
	
	gcmControlRegister volatile *ctrl = gcmGetControlRegister();
	ctrl->put = startoffs;
	
	PS3SessionLog_event("RSX DIAG: Waiting for get == %08X, current get is %08X, current put is %08X", startoffs, ctrl->get, ctrl->put);
	
	u32 timeout = 0;
	while (ctrl->get != startoffs) {
		usleep(30);
		timeout++;
		if (timeout > 100000) { // 3 seconds
			PS3SessionLog_event("RSX_PANIC: command buffer get wait timeout! get: %08X, put: %08X", ctrl->get, ctrl->put);
			PS3SessionLog_flushToDisk();
			exit(1);
		}
	}
	PS3SessionLog_event("RSX DIAG: resetCommandBuffer finished waiting. get is %08X", ctrl->get);
	
	// Now safe to reset the CPU pointers
	context->current = initialCommandBuffer;
	context->begin   = initialCommandBuffer;
	context->end     = context->begin + commandBufferSize;
}

void init_screen(void *host_addr,u32 size)
{
	rsxInit(&context,CB_SIZE,size,host_addr);

	initialCommandBuffer = context->current;
	commandBufferSize = CB_SIZE;
	videoOutState state;
	videoOutGetState(0,0,&state);

	videoOutGetResolution(state.displayMode.resolution,&res);

	videoOutConfiguration vconfig;
	memset(&vconfig,0,sizeof(videoOutConfiguration));

	vconfig.resolution = state.displayMode.resolution;
	vconfig.format = VIDEO_OUT_BUFFER_FORMAT_XRGB;
	vconfig.pitch = res.width*sizeof(u32);

	waitRSXIdle();

	videoOutConfigure(0,&vconfig,NULL,0);
	videoOutGetState(0,0,&state);

	gcmSetFlipMode(GCM_FLIP_VSYNC);

	display_width = res.width;
	display_height = res.height;

	color_pitch = display_width*sizeof(u32);
	color_buffer[0] = (u32*)rsxMemalign(64,(display_height*color_pitch));
	color_buffer[1] = (u32*)rsxMemalign(64,(display_height*color_pitch));

	rsxAddressToOffset(color_buffer[0],&color_offset[0]);
	rsxAddressToOffset(color_buffer[1],&color_offset[1]);

	gcmSetDisplayBuffer(0,color_offset[0],color_pitch,display_width,display_height);
	gcmSetDisplayBuffer(1,color_offset[1],color_pitch,display_width,display_height);

	depth_pitch = display_width*sizeof(u32);
	depth_buffer = (u32*)rsxMemalign(64,(display_height*depth_pitch)*2);
	rsxAddressToOffset(depth_buffer,&depth_offset);
}

void flip()
{
	PS3SessionLog_event("RSX DIAG: flip() start. ctx->current alignment: %d", ((uintptr_t)context->current) % 4);
	rsxFinish(context, 0);
	if(!first_fb) waitflip();
	else gcmResetFlipStatus();

	gcmSetFlip(context,curr_fb);
	rsxFlushBuffer(context);

	gcmSetWaitFlip(context);

	curr_fb ^= 1;
	if ((uintptr_t)context->current + 512 * 1024 >= (uintptr_t)context->end) {
		resetCommandBuffer();
	}
	setRenderTarget(curr_fb);

	first_fb = 0;
	PS3SessionLog_event("RSX DIAG: flip() end.");
}
