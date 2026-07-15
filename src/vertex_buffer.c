#include "vertex_buffer.h"
#include "utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VERTEX_MAX_FORMATS 64
#define VERTEX_MAX_BUFFERS 128
#define VERTEX_INITIAL_CAPACITY 8
#define VERTEX_MAX_PER_BUFFER 16384

enum {
    VF_POSITION = 1u << 0,
    VF_POSITION3D = 1u << 1,
    VF_COLOR = 1u << 2,
    VF_NORMAL = 1u << 3,
    VF_TEXCOORD = 1u << 4
};

typedef struct {
    bool inUse;
    uint32_t attributes;
} VertexFormat;

typedef struct {
    bool inUse;
    int32_t formatId;
    RendererVertex2D* vertices;
    int32_t count;
    int32_t capacity;
    uint32_t pendingColor;
    float pendingAlpha;
} VertexBuffer;

struct VertexManager {
    VertexFormat formats[VERTEX_MAX_FORMATS];
    VertexBuffer buffers[VERTEX_MAX_BUFFERS];
    uint32_t pendingFormat;
};

static VertexBuffer* getBuffer(VertexManager* manager, int32_t id) {
    if (manager == nullptr || id < 0 || id >= VERTEX_MAX_BUFFERS || !manager->buffers[id].inUse) return nullptr;
    return &manager->buffers[id];
}

static RendererVertex2D* appendVertex(VertexBuffer* buffer, float x, float y) {
    if (buffer == nullptr || buffer->count >= VERTEX_MAX_PER_BUFFER) return nullptr;
    if (buffer->count == buffer->capacity) {
        int32_t newCapacity = buffer->capacity == 0 ? VERTEX_INITIAL_CAPACITY : buffer->capacity * 2;
        if (newCapacity > VERTEX_MAX_PER_BUFFER) newCapacity = VERTEX_MAX_PER_BUFFER;
        buffer->vertices = (RendererVertex2D*) safeRealloc(buffer->vertices,
            (size_t) newCapacity * sizeof(RendererVertex2D));
        buffer->capacity = newCapacity;
    }
    RendererVertex2D* vertex = &buffer->vertices[buffer->count++];
    memset(vertex, 0, sizeof(*vertex));
    vertex->x = x;
    vertex->y = y;
    vertex->q = 1.0f;
    vertex->color = buffer->pendingColor;
    vertex->alpha = buffer->pendingAlpha;
    return vertex;
}

VertexManager* VertexManager_create(void) {
    return (VertexManager*) safeCalloc(1, sizeof(VertexManager));
}

void VertexManager_free(VertexManager* manager) {
    if (manager == nullptr) return;
    for (int32_t i = 0; i < VERTEX_MAX_BUFFERS; ++i) free(manager->buffers[i].vertices);
    free(manager);
}

void VertexManager_formatBegin(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat = 0; }
void VertexManager_formatAddPosition(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat |= VF_POSITION; }
void VertexManager_formatAddPosition3D(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat |= VF_POSITION3D; }
void VertexManager_formatAddColor(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat |= VF_COLOR; }
void VertexManager_formatAddNormal(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat |= VF_NORMAL; }
void VertexManager_formatAddTexcoord(VertexManager* manager) { if (manager != nullptr) manager->pendingFormat |= VF_TEXCOORD; }

int32_t VertexManager_formatEnd(VertexManager* manager) {
    if (manager == nullptr) return -1;
    for (int32_t i = 0; i < VERTEX_MAX_FORMATS; ++i) {
        if (!manager->formats[i].inUse) {
            manager->formats[i].inUse = true;
            manager->formats[i].attributes = manager->pendingFormat;
            return i;
        }
    }
    return -1;
}

int32_t VertexManager_createBuffer(VertexManager* manager) {
    if (manager == nullptr) return -1;
    for (int32_t i = 0; i < VERTEX_MAX_BUFFERS; ++i) {
        if (!manager->buffers[i].inUse) {
            VertexBuffer* buffer = &manager->buffers[i];
            buffer->inUse = true;
            buffer->formatId = -1;
            buffer->count = 0;
            buffer->pendingColor = 0xFFFFFFu;
            buffer->pendingAlpha = 1.0f;
            return i;
        }
    }
    return -1;
}

void VertexManager_deleteBuffer(VertexManager* manager, int32_t bufferId) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr) return;
    free(buffer->vertices);
    memset(buffer, 0, sizeof(*buffer));
}

void VertexManager_begin(VertexManager* manager, int32_t bufferId, int32_t formatId) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr) return;
    buffer->formatId = formatId;
    buffer->count = 0;
    buffer->pendingColor = 0xFFFFFFu;
    buffer->pendingAlpha = 1.0f;
}

void VertexManager_color(VertexManager* manager, int32_t bufferId, uint32_t color, float alpha) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr) return;
    buffer->pendingColor = color;
    buffer->pendingAlpha = alpha;
}

void VertexManager_position(VertexManager* manager, int32_t bufferId, float x, float y) {
    appendVertex(getBuffer(manager, bufferId), x, y);
}

void VertexManager_position3D(VertexManager* manager, int32_t bufferId, float x, float y, MAYBE_UNUSED float z) {
    appendVertex(getBuffer(manager, bufferId), x, y);
}

void VertexManager_normal(VertexManager* manager, int32_t bufferId, float nx, float ny, float nz) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr || buffer->count == 0) return;
    RendererVertex2D* vertex = &buffer->vertices[buffer->count - 1];
    vertex->q = fabsf(nz) > 0.000001f ? nz : 1.0f;
    vertex->u = nx / vertex->q;
    vertex->v = ny / vertex->q;
}

void VertexManager_texcoord(VertexManager* manager, int32_t bufferId, float u, float v) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr || buffer->count == 0) return;
    buffer->vertices[buffer->count - 1].u = u;
    buffer->vertices[buffer->count - 1].v = v;
    buffer->vertices[buffer->count - 1].q = 1.0f;
}

void VertexManager_end(MAYBE_UNUSED VertexManager* manager, MAYBE_UNUSED int32_t bufferId) {}

void VertexManager_submit(VertexManager* manager, Renderer* renderer, int32_t bufferId,
                          int32_t primitiveKind, uint32_t textureHandle) {
    VertexBuffer* buffer = getBuffer(manager, bufferId);
    if (buffer == nullptr || renderer == nullptr || renderer->vtable == nullptr ||
        renderer->vtable->drawTexturedVertices == nullptr || buffer->count == 0) return;
    renderer->vtable->drawTexturedVertices(renderer, textureHandle, buffer->vertices,
                                           buffer->count, primitiveKind);
}
