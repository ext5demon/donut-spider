#include "particle_system.h"

#include "renderer.h"
#include "runner.h"
#include "stb_ds.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    bool freed;
    int32_t sprite;
    bool animate;
    bool stretch;
    bool randomFrame;
    float speedMin, speedMax, speedIncrement, speedWiggle;
    float directionMin, directionMax, directionIncrement, directionWiggle;
    int32_t lifeMin, lifeMax;
    float alphaStart, alphaMiddle, alphaEnd;
    float sizeMin, sizeMax, sizeIncrement, sizeWiggle;
    float gravityAmount, gravityDirection;
    bool additive;
} ParticleType;

typedef struct {
    float x, y;
    float speed;
    float direction;
    float size;
    int32_t typeId;
    int32_t age;
    int32_t life;
    int32_t subimage;
} Particle;

typedef struct {
    float xmin, xmax, ymin, ymax;
    int32_t shape;
    int32_t distribution;
    int32_t typeId;
    int32_t streamCount;
} ParticleEmitter;

typedef struct {
    bool freed;
    bool automaticDraw;
    bool automaticUpdate;
    int32_t depth;
    Particle* particles;
    ParticleEmitter* emitters;
} ParticleSystem;

struct ParticleManager {
    ParticleType* types;
    ParticleSystem* systems;
};

static float randomUnit(void) {
    return (float) rand() / ((float) RAND_MAX + 1.0f);
}

static float randomRange(float minValue, float maxValue) {
    if (maxValue < minValue) {
        float swap = minValue;
        minValue = maxValue;
        maxValue = swap;
    }
    return minValue + (maxValue - minValue) * randomUnit();
}

static ParticleType* getType(ParticleManager* manager, int32_t typeId) {
    if (manager == nullptr || typeId < 0 || typeId >= (int32_t) arrlen(manager->types)) return nullptr;
    ParticleType* type = &manager->types[typeId];
    return type->freed ? nullptr : type;
}

static ParticleSystem* getSystem(ParticleManager* manager, int32_t systemId) {
    if (manager == nullptr || systemId < 0 || systemId >= (int32_t) arrlen(manager->systems)) return nullptr;
    ParticleSystem* system = &manager->systems[systemId];
    return system->freed ? nullptr : system;
}

static ParticleEmitter* getEmitter(ParticleSystem* system, int32_t emitterId) {
    if (system == nullptr || emitterId < 0 || emitterId >= (int32_t) arrlen(system->emitters)) return nullptr;
    return &system->emitters[emitterId];
}

ParticleManager* ParticleManager_create(void) {
    return (ParticleManager*) calloc(1, sizeof(ParticleManager));
}

void ParticleManager_free(ParticleManager* manager) {
    if (manager == nullptr) return;
    repeat((int32_t) arrlen(manager->systems), i) {
        arrfree(manager->systems[i].particles);
        arrfree(manager->systems[i].emitters);
    }
    arrfree(manager->systems);
    arrfree(manager->types);
    free(manager);
}

static ParticleManager* requireManager(ParticleManager* manager) {
    return manager;
}

int32_t ParticleManager_createSystem(ParticleManager* manager) {
    if (requireManager(manager) == nullptr) return -1;
    repeat((int32_t) arrlen(manager->systems), i) {
        if (manager->systems[i].freed) {
            ParticleSystem replacement = {0};
            replacement.automaticDraw = true;
            replacement.automaticUpdate = true;
            manager->systems[i] = replacement;
            return i;
        }
    }
    ParticleSystem system = {0};
    system.automaticDraw = true;
    system.automaticUpdate = true;
    arrput(manager->systems, system);
    return (int32_t) arrlen(manager->systems) - 1;
}

void ParticleManager_destroySystem(ParticleManager* manager, int32_t systemId) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system == nullptr) return;
    arrfree(system->particles);
    arrfree(system->emitters);
    *system = (ParticleSystem) {0};
    system->freed = true;
}

void ParticleManager_setAutomaticDraw(ParticleManager* manager, int32_t systemId, bool enabled) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system != nullptr) system->automaticDraw = enabled;
}

void ParticleManager_setSystemDepth(ParticleManager* manager, int32_t systemId, int32_t depth) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system != nullptr) system->depth = depth;
}

int32_t ParticleManager_createType(ParticleManager* manager) {
    if (requireManager(manager) == nullptr) return -1;
    ParticleType type = {0};
    type.sprite = -1;
    type.lifeMin = 100;
    type.lifeMax = 100;
    type.alphaStart = type.alphaMiddle = type.alphaEnd = 1.0f;
    type.sizeMin = type.sizeMax = 1.0f;
    repeat((int32_t) arrlen(manager->types), i) {
        if (manager->types[i].freed) {
            manager->types[i] = type;
            return i;
        }
    }
    arrput(manager->types, type);
    return (int32_t) arrlen(manager->types) - 1;
}

void ParticleManager_destroyType(ParticleManager* manager, int32_t typeId) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    *type = (ParticleType) {0};
    type->freed = true;
}

void ParticleManager_setTypeSprite(ParticleManager* manager, int32_t typeId, int32_t sprite,
                                   bool animate, bool stretch, bool randomFrame) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->sprite = sprite;
    type->animate = animate;
    type->stretch = stretch;
    type->randomFrame = randomFrame;
}

void ParticleManager_setTypeSpeed(ParticleManager* manager, int32_t typeId, float minValue,
                                  float maxValue, float increment, float wiggle) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->speedMin = minValue;
    type->speedMax = maxValue;
    type->speedIncrement = increment;
    type->speedWiggle = wiggle;
}

void ParticleManager_setTypeDirection(ParticleManager* manager, int32_t typeId, float minValue,
                                      float maxValue, float increment, float wiggle) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->directionMin = minValue;
    type->directionMax = maxValue;
    type->directionIncrement = increment;
    type->directionWiggle = wiggle;
}

void ParticleManager_setTypeLife(ParticleManager* manager, int32_t typeId, int32_t minValue,
                                 int32_t maxValue) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    if (maxValue < minValue) {
        int32_t swap = minValue;
        minValue = maxValue;
        maxValue = swap;
    }
    type->lifeMin = minValue < 1 ? 1 : minValue;
    type->lifeMax = maxValue < type->lifeMin ? type->lifeMin : maxValue;
}

void ParticleManager_setTypeAlpha3(ParticleManager* manager, int32_t typeId, float start,
                                   float middle, float end) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->alphaStart = start;
    type->alphaMiddle = middle;
    type->alphaEnd = end;
}

void ParticleManager_setTypeSize(ParticleManager* manager, int32_t typeId, float minValue,
                                 float maxValue, float increment, float wiggle) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->sizeMin = minValue;
    type->sizeMax = maxValue;
    type->sizeIncrement = increment;
    type->sizeWiggle = wiggle;
}

void ParticleManager_setTypeBlend(ParticleManager* manager, int32_t typeId, bool additive) {
    ParticleType* type = getType(manager, typeId);
    if (type != nullptr) type->additive = additive;
}

void ParticleManager_setTypeGravity(ParticleManager* manager, int32_t typeId, float amount,
                                    float direction) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr) return;
    type->gravityAmount = amount;
    type->gravityDirection = direction;
}

int32_t ParticleManager_createEmitter(ParticleManager* manager, int32_t systemId) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system == nullptr) return -1;
    ParticleEmitter emitter = {0};
    emitter.typeId = -1;
    arrput(system->emitters, emitter);
    return (int32_t) arrlen(system->emitters) - 1;
}

void ParticleManager_destroyAllEmitters(ParticleManager* manager, int32_t systemId) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system == nullptr) return;
    arrfree(system->emitters);
    system->emitters = nullptr;
}

void ParticleManager_setEmitterRegion(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                      float xmin, float xmax, float ymin, float ymax,
                                      int32_t shape, int32_t distribution) {
    ParticleEmitter* emitter = getEmitter(getSystem(manager, systemId), emitterId);
    if (emitter == nullptr) return;
    emitter->xmin = xmin;
    emitter->xmax = xmax;
    emitter->ymin = ymin;
    emitter->ymax = ymax;
    emitter->shape = shape;
    emitter->distribution = distribution;
}

static void spawnParticle(ParticleManager* manager, ParticleSystem* system,
                          const ParticleEmitter* emitter, int32_t typeId) {
    ParticleType* type = getType(manager, typeId);
    if (type == nullptr || system == nullptr || emitter == nullptr) return;

    Particle particle = {0};
    particle.x = randomRange(emitter->xmin, emitter->xmax);
    particle.y = randomRange(emitter->ymin, emitter->ymax);
    particle.speed = randomRange(type->speedMin, type->speedMax);
    particle.direction = randomRange(type->directionMin, type->directionMax);
    particle.size = randomRange(type->sizeMin, type->sizeMax);
    particle.typeId = typeId;
    particle.life = type->lifeMin;
    if (type->lifeMax > type->lifeMin) {
        particle.life += rand() % (type->lifeMax - type->lifeMin + 1);
    }
    particle.subimage = type->randomFrame ? rand() : 0;
    arrput(system->particles, particle);
}

static void emitCount(ParticleManager* manager, ParticleSystem* system,
                      const ParticleEmitter* emitter, int32_t typeId, int32_t count) {
    if (count > 0) {
        repeat(count, i) spawnParticle(manager, system, emitter, typeId);
    } else if (count < 0 && (rand() % -count) == 0) {
        spawnParticle(manager, system, emitter, typeId);
    }
}

void ParticleManager_streamEmitter(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                   int32_t typeId, int32_t count) {
    ParticleEmitter* emitter = getEmitter(getSystem(manager, systemId), emitterId);
    if (emitter == nullptr || getType(manager, typeId) == nullptr) return;
    emitter->typeId = typeId;
    emitter->streamCount = count;
}

void ParticleManager_burstEmitter(ParticleManager* manager, int32_t systemId, int32_t emitterId,
                                  int32_t typeId, int32_t count) {
    ParticleSystem* system = getSystem(manager, systemId);
    ParticleEmitter* emitter = getEmitter(system, emitterId);
    if (emitter == nullptr) return;
    emitCount(manager, system, emitter, typeId, count);
}

static void updateOneSystem(ParticleManager* manager, ParticleSystem* system) {
    repeat((int32_t) arrlen(system->emitters), i) {
        ParticleEmitter* emitter = &system->emitters[i];
        if (emitter->typeId >= 0 && emitter->streamCount != 0) {
            emitCount(manager, system, emitter, emitter->typeId, emitter->streamCount);
        }
    }

    int32_t i = 0;
    while (i < (int32_t) arrlen(system->particles)) {
        Particle* particle = &system->particles[i];
        ParticleType* type = getType(manager, particle->typeId);
        if (type == nullptr || ++particle->age >= particle->life) {
            arrdelswap(system->particles, i);
            continue;
        }

        particle->speed += type->speedIncrement;
        particle->direction += type->directionIncrement;
        particle->size += type->sizeIncrement;

        float directionRadians = particle->direction * (float) (M_PI / 180.0);
        particle->x += cosf(directionRadians) * particle->speed;
        particle->y -= sinf(directionRadians) * particle->speed;

        if (type->gravityAmount != 0.0f) {
            float gravityRadians = type->gravityDirection * (float) (M_PI / 180.0);
            particle->x += cosf(gravityRadians) * type->gravityAmount;
            particle->y -= sinf(gravityRadians) * type->gravityAmount;
        }
        i++;
    }
}

void ParticleManager_updateSystem(ParticleManager* manager, int32_t systemId) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system != nullptr) updateOneSystem(manager, system);
}

void ParticleManager_updateAutomatic(ParticleManager* manager) {
    if (manager == nullptr) return;
    repeat((int32_t) arrlen(manager->systems), i) {
        ParticleSystem* system = &manager->systems[i];
        if (!system->freed && system->automaticUpdate) updateOneSystem(manager, system);
    }
}

static float particleAlpha(const ParticleType* type, const Particle* particle) {
    float progress = particle->life > 0 ? (float) particle->age / (float) particle->life : 1.0f;
    if (progress < 0.5f) {
        return type->alphaStart + (type->alphaMiddle - type->alphaStart) * (progress * 2.0f);
    }
    return type->alphaMiddle + (type->alphaEnd - type->alphaMiddle) * ((progress - 0.5f) * 2.0f);
}

void ParticleManager_drawSystem(ParticleManager* manager, Runner* runner, int32_t systemId) {
    ParticleSystem* system = getSystem(manager, systemId);
    if (system == nullptr || runner == nullptr || runner->renderer == nullptr) return;
    Renderer* renderer = runner->renderer;
    int32_t originalBlendMode = renderer->vtable->gpuGetBlendMode(renderer);
    bool additive = false;

    repeat((int32_t) arrlen(system->particles), i) {
        Particle* particle = &system->particles[i];
        ParticleType* type = getType(manager, particle->typeId);
        if (type == nullptr || type->sprite < 0 ||
            (uint32_t) type->sprite >= runner->dataWin->sprt.count) continue;

        if (type->additive != additive) {
            renderer->vtable->gpuSetBlendMode(renderer, type->additive ? bm_add : bm_normal);
            additive = type->additive;
        }

        int32_t subimage = particle->subimage;
        if (type->animate) subimage += particle->age;
        Renderer_drawSpriteExt(renderer, type->sprite, subimage, particle->x, particle->y,
                               particle->size, particle->size, 0.0f, 0xFFFFFF,
                               particleAlpha(type, particle));
    }

    renderer->vtable->gpuSetBlendMode(renderer, originalBlendMode);
}

void ParticleManager_drawAutomatic(ParticleManager* manager, Runner* runner) {
    if (manager == nullptr) return;
    repeat((int32_t) arrlen(manager->systems), i) {
        ParticleSystem* system = &manager->systems[i];
        if (!system->freed && system->automaticDraw) ParticleManager_drawSystem(manager, runner, i);
    }
}
