#include "vertex_buffer.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static RendererVertex2D captured[4];
static int32_t capturedCount;
static int32_t capturedPrimitive;
static uint32_t capturedTexture;

static void captureVertices(MAYBE_UNUSED Renderer* renderer, uint32_t texture,
                            const RendererVertex2D* vertices, int32_t count,
                            int32_t primitive) {
    assert(count == 4);
    memcpy(captured, vertices, sizeof(captured));
    capturedCount = count;
    capturedPrimitive = primitive;
    capturedTexture = texture;
}

int main(void) {
    VertexManager* manager = VertexManager_create();
    assert(manager != nullptr);

    VertexManager_formatBegin(manager);
    VertexManager_formatAddColor(manager);
    VertexManager_formatAddPosition(manager);
    VertexManager_formatAddNormal(manager);
    int32_t format = VertexManager_formatEnd(manager);
    int32_t buffer = VertexManager_createBuffer(manager);
    assert(format >= 0 && buffer >= 0);
    VertexManager_begin(manager, buffer, format);

    for (int32_t i = 0; i < 4; ++i) {
        VertexManager_color(manager, buffer, 0x112233u + (uint32_t) i, 0.75f);
        VertexManager_position(manager, buffer, (float) i * 10.0f, (float) i * 20.0f);
        VertexManager_normal(manager, buffer, (float) (i + 1) * 2.0f,
                            (float) (i + 2) * 2.0f, 2.0f);
    }
    VertexManager_end(manager, buffer);

    RendererVtable vtable = {0};
    vtable.drawTexturedVertices = captureVertices;
    Renderer renderer = {0};
    renderer.vtable = &vtable;
    VertexManager_submit(manager, &renderer, buffer, 5, 123u);

    assert(capturedCount == 4);
    assert(capturedPrimitive == 5);
    assert(capturedTexture == 123u);
    assert(captured[3].x == 30.0f && captured[3].y == 60.0f);
    assert(fabsf(captured[3].u - 4.0f) < 0.00001f);
    assert(fabsf(captured[3].v - 5.0f) < 0.00001f);
    assert(fabsf(captured[3].q - 2.0f) < 0.00001f);
    assert(captured[3].color == 0x112236u);
    assert(fabsf(captured[3].alpha - 0.75f) < 0.00001f);

    VertexManager_deleteBuffer(manager, buffer);
    VertexManager_free(manager);
    return 0;
}
