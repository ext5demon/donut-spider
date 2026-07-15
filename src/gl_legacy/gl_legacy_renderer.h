#ifndef _BS_GL_LEGACY_RENDERER_H_
#define _BS_GL_LEGACY_RENDERER_H_

#include "common.h"
#include "renderer.h"
#include "runner.h"
#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#else
#include <glad/glad.h>
#endif

// ===[ GLLegacyRenderer Struct ]===
// Exposed in the header so platform-specific code (main.c) can access FBO fields for screenshots.
typedef struct {
    Renderer base; // Must be first field for struct embedding

    GLuint* glTextures;       // one GL texture per TXTR page
    int32_t* textureWidths;   // needed for UV normalization
    int32_t* textureHeights;
    bool* textureLoaded;      // lazy loading: true once PNG decoded and uploaded
    uint64_t* textureLastUsed;
    size_t* textureBytes;
    uint32_t textureCount;
    uint64_t textureUseCounter;
    size_t textureBytesResident;
    size_t textureCacheBudget;

    GLuint whiteTexture; // 1x1 white pixel for drawing primitives (rectangles, lines, etc.)

    int32_t windowW; // stored from beginFrame for endFrame blit
    int32_t windowH;
    int32_t gameW; // game width (matches the application_surface size)
    int32_t gameH; // game height (matches the application_surface size)

    // Original counts from data.win (dynamic slots start at these indices)
    uint32_t originalTexturePageCount;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;

    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;

    // GML surfaces (each is an FBO with a backing color texture)
    GLuint* surfaces;
    GLuint* surfaceTexture;
    int32_t* surfaceWidth;
    int32_t* surfaceHeight;
    uint32_t surfaceCount;

    // Blending mode + factors
    int32_t currentBlendMode;
    int32_t currentSFactor;
    int32_t currentDFactor;
    int32_t currentSFactorAlpha;
    int32_t currentDFactorAlpha;

    // Development diagnostics. These are populated by the surface paths and
    // displayed by the PS3 debug overlay; release builds simply ignore them.
    int32_t activeSurfaceId;
    uint32_t captureSerial;
    int32_t lastCaptureSurfaceId;
    int32_t lastCaptureX;
    int32_t lastCaptureY;
    int32_t lastCaptureWidth;
    int32_t lastCaptureHeight;
    uint8_t lastCaptureAlphaMin;
    uint8_t lastCaptureAlphaMax;
    uint32_t lastCaptureAlphaZero;
    uint32_t lastCaptureAlphaPartial;
    uint32_t lastCaptureAlphaOpaque;
    bool lastCaptureRemoveBack;
    bool lastCaptureSmooth;
    uint8_t lastCaptureBackgroundR;
    uint8_t lastCaptureBackgroundG;
    uint8_t lastCaptureBackgroundB;
    uint8_t lastCaptureOutputAlphaMin;
    uint8_t lastCaptureOutputAlphaMax;
    uint32_t lastCaptureOutputAlphaZero;
    uint32_t lastCaptureOutputAlphaPartial;
    uint32_t lastCaptureOutputAlphaOpaque;
    bool lastCaptureBlendEnabled;
    bool lastCaptureAlphaTestEnabled;
    bool lastCaptureWriteAlpha;
    int32_t lastCaptureBlendMode;
    int32_t lastCaptureSFactor;
    int32_t lastCaptureDFactor;
    int32_t lastCaptureSFactorAlpha;
    int32_t lastCaptureDFactorAlpha;
} GLLegacyRenderer;

bool GLLegacyRenderer_ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId);
Renderer* GLLegacyRenderer_create(void);

#endif /* _BS_GL_LEGACY_RENDERER_H_ */
