#include <stdio.h>
#include <stdlib.h>

#include "data_win.h"
#include "gml_array.h"
#include "gml_gc.h"
#include "instance.h"
#include "runner.h"
#include "stb_ds.h"
#include "vm.h"

static Instance* createTrackedStruct(Runner* runner, uint32_t id) {
    Instance* instance = Instance_create(id, STRUCT_OBJECT_INDEX, 0, 0);
    instance->refCount = 1; // registry reference
    instance->structRegistryIndex = (int32_t) arrlen(runner->structInstances);
    arrput(runner->structInstances, instance);
    hmput(runner->instancesById, instance->instanceId, instance);
    return instance;
}

static void instanceOwnsArray(Instance* instance, int32_t key, GMLArray* array) {
    RValue value = RValue_makeArray(array);
    Instance_setSelfVar(instance, key, value);
    RValue_free(&value);
}

static void arrayOwnsStruct(GMLArray* array, Instance* instance) {
    RValue value = RValue_makeStructAndIncRef(instance);
    GMLArray_add(array, value);
    RValue_free(&value);
}

static int expectStats(const char* name, GMLGCStats stats, size_t arrays, size_t structs) {
    if (stats.arraysCollected == arrays && stats.structsCollected == structs) return 0;
    fprintf(stderr,
            "%s: expected arrays=%zu structs=%zu, got arrays=%zu structs=%zu\n",
            name, arrays, structs, stats.arraysCollected, stats.structsCollected);
    return 1;
}

int main(void) {
    DataWin dataWin = {0};
    VMContext vm = {0};
    Runner runner = {0};
    vm.dataWin = &dataWin;
    vm.runner = &runner;
    runner.dataWin = &dataWin;
    runner.vmContext = &vm;

    // Two structs and two arrays form an unreachable four-node cycle.
    Instance* a = createTrackedStruct(&runner, 1001);
    Instance* b = createTrackedStruct(&runner, 1002);
    GMLArray* ab = GMLArray_create(17, 0);
    GMLArray* ba = GMLArray_create(17, 0);
    instanceOwnsArray(a, 1, ab);
    instanceOwnsArray(b, 1, ba);
    arrayOwnsStruct(ab, b);
    arrayOwnsStruct(ba, a);

    if (expectStats("unreachable cycle", GMLGC_collect(&runner), 2, 2)) return 1;
    if (GMLArray_gcTrackedCount() != 0 || arrlen(runner.structInstances) != 0) return 2;

    // The same kind of cycle must survive while reachable from a game object.
    Instance* root = Instance_create(2001, 0, 0, 0);
    arrput(runner.instances, root);
    Instance* live = createTrackedStruct(&runner, 1003);
    GMLArray* selfCycle = GMLArray_create(17, 0);
    instanceOwnsArray(live, 2, selfCycle);
    arrayOwnsStruct(selfCycle, live);
    RValue liveValue = RValue_makeStructAndIncRef(live);
    Instance_setSelfVar(root, 3, liveValue);
    RValue_free(&liveValue);

    if (expectStats("reachable cycle", GMLGC_collect(&runner), 0, 0)) return 3;
    if (GMLArray_gcTrackedCount() != 1 || arrlen(runner.structInstances) != 1) return 4;

    // Removing the only root makes the cycle collectible on the next pass.
    Instance_freeContents(root);
    arrfree(runner.instances);
    runner.instances = nullptr;
    free(root);
    if (expectStats("released cycle", GMLGC_collect(&runner), 1, 1)) return 5;
    if (GMLArray_gcTrackedCount() != 0 || arrlen(runner.structInstances) != 0) return 6;

    hmfree(runner.instancesById);
    arrfree(runner.instances);
    arrfree(runner.structInstances);
    puts("gml_gc_cycle_test: passed");
    return 0;
}
