#ifndef _BS_PARTICLE_SYSTEM_H_
#define _BS_PARTICLE_SYSTEM_H_

#include "common.h"
#include <stdint.h>

#ifndef RUNNER_DEFINED
#define RUNNER_DEFINED
typedef struct Runner Runner;
#endif
#ifndef PARTICLE_MANAGER_DEFINED
#define PARTICLE_MANAGER_DEFINED
typedef struct ParticleManager ParticleManager;
#endif

ParticleManager* ParticleManager_create(void);
void ParticleManager_free(ParticleManager* manager);

int32_t ParticleManager_createSystem(ParticleManager* manager);
void ParticleManager_destroySystem(ParticleManager* manager, int32_t systemId);
void ParticleManager_setAutomaticDraw(ParticleManager* manager, int32_t systemId, bool enabled);
void ParticleManager_setSystemDepth(ParticleManager* manager, int32_t systemId, int32_t depth);
void ParticleManager_updateSystem(ParticleManager* manager, int32_t systemId);
void ParticleManager_drawSystem(ParticleManager* manager, Runner* runner, int32_t systemId);
void ParticleManager_updateAutomatic(ParticleManager* manager);
void ParticleManager_drawAutomatic(ParticleManager* manager, Runner* runner);

int32_t ParticleManager_createType(ParticleManager* manager);
void ParticleManager_destroyType(ParticleManager* manager, int32_t typeId);
void ParticleManager_setTypeSprite(ParticleManager* manager, int32_t typeId, int32_t sprite,
                                   bool animate, bool stretch, bool randomFrame);
void ParticleManager_setTypeSpeed(ParticleManager* manager, int32_t typeId, float minValue,
                                  float maxValue, float increment, float wiggle);
void ParticleManager_setTypeDirection(ParticleManager* manager, int32_t typeId, float minValue,
                                      float maxValue, float increment, float wiggle);
void ParticleManager_setTypeLife(ParticleManager* manager, int32_t typeId, int32_t minValue,
                                 int32_t maxValue);
void ParticleManager_setTypeAlpha3(ParticleManager* manager, int32_t typeId, float start,
                                   float middle, float end);
void ParticleManager_setTypeSize(ParticleManager* manager, int32_t typeId, float minValue,
                                 float maxValue, float increment, float wiggle);
void ParticleManager_setTypeBlend(ParticleManager* manager, int32_t typeId, bool additive);
void ParticleManager_setTypeGravity(ParticleManager* manager, int32_t typeId, float amount,
                                    float direction);

int32_t ParticleManager_createEmitter(ParticleManager* manager, int32_t systemId);
void ParticleManager_destroyAllEmitters(ParticleManager* manager, int32_t systemId);
void ParticleManager_setEmitterRegion(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                      float xmin, float xmax, float ymin, float ymax,
                                      int32_t shape, int32_t distribution);
void ParticleManager_streamEmitter(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                   int32_t typeId, int32_t count);
void ParticleManager_burstEmitter(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                  int32_t typeId, int32_t count);

#endif
