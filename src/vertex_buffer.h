#ifndef _BS_VERTEX_BUFFER_H_
#define _BS_VERTEX_BUFFER_H_

#include "common.h"
#include "renderer.h"
#include <stdint.h>

#ifndef VERTEX_MANAGER_DEFINED
#define VERTEX_MANAGER_DEFINED
typedef struct VertexManager VertexManager;
#endif

VertexManager* VertexManager_create(void);
void VertexManager_free(VertexManager* manager);

void VertexManager_formatBegin(VertexManager* manager);
void VertexManager_formatAddPosition(VertexManager* manager);
void VertexManager_formatAddPosition3D(VertexManager* manager);
void VertexManager_formatAddColor(VertexManager* manager);
void VertexManager_formatAddNormal(VertexManager* manager);
void VertexManager_formatAddTexcoord(VertexManager* manager);
int32_t VertexManager_formatEnd(VertexManager* manager);

int32_t VertexManager_createBuffer(VertexManager* manager);
void VertexManager_deleteBuffer(VertexManager* manager, int32_t bufferId);
void VertexManager_begin(VertexManager* manager, int32_t bufferId, int32_t formatId);
void VertexManager_color(VertexManager* manager, int32_t bufferId, uint32_t color, float alpha);
void VertexManager_position(VertexManager* manager, int32_t bufferId, float x, float y);
void VertexManager_position3D(VertexManager* manager, int32_t bufferId, float x, float y, float z);
void VertexManager_normal(VertexManager* manager, int32_t bufferId, float nx, float ny, float nz);
void VertexManager_texcoord(VertexManager* manager, int32_t bufferId, float u, float v);
void VertexManager_end(VertexManager* manager, int32_t bufferId);
void VertexManager_submit(VertexManager* manager, Renderer* renderer, int32_t bufferId,
                          int32_t primitiveKind, uint32_t textureHandle);

#endif
