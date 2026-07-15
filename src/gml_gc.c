#include "gml_gc.h"

#include <stdlib.h>

#include "gml_array.h"
#include "gml_method.h"
#include "instance.h"
#include "int_rvalue_hashmap.h"
#include "runner.h"
#include "stb_ds.h"
#include "utils.h"
#include "vm.h"

static void markRValue(Runner* runner, const RValue* value);

static void markInstanceValues(Runner* runner, Instance* instance) {
    if (instance == nullptr) return;
    repeat(instance->selfVars.capacity, i) {
        IntRValueEntry* entry = &instance->selfVars.entries[i];
        if (entry->key != INT_RVALUE_HASHMAP_EMPTY_KEY) markRValue(runner, &entry->value);
    }
}

static void markStruct(Runner* runner, Instance* instance) {
    if (instance == nullptr || instance->objectIndex != STRUCT_OBJECT_INDEX || instance->gcMarked) return;
    instance->gcMarked = true;
    markStruct(runner, instance->staticParent);
    markInstanceValues(runner, instance);
}

static void markArray(Runner* runner, GMLArray* array) {
    if (array == nullptr || array->gcMarked) return;
    array->gcMarked = true;
    if (array->type == GML_LEGACY_ARRAY) {
        repeat(array->legacy.rowCount, rowIndex) {
            GMLArrayRow* row = &array->legacy.rows[rowIndex];
            repeat(row->length, i) markRValue(runner, &row->data[i]);
        }
    } else {
        repeat(array->modern.length, i) markRValue(runner, &array->modern.data[i]);
    }
}

static void markRValue(Runner* runner, const RValue* value) {
    if (value == nullptr) return;
    if (value->type == RVALUE_ARRAY) {
        markArray(runner, value->array);
    } else if (value->type == RVALUE_STRUCT) {
        markStruct(runner, value->structInst);
#if IS_WAD17_OR_HIGHER_ENABLED
    } else if (value->type == RVALUE_METHOD && value->method != nullptr) {
        Instance* bound = hmget(runner->instancesById, value->method->boundInstanceId);
        markStruct(runner, bound);
#endif
    }
}

static void markVMRoots(Runner* runner) {
    VMContext* vm = runner->vmContext;
    markInstanceValues(runner, vm->globalScopeInstance);
    markStruct(runner, vm->currentInstance);
    markStruct(runner, vm->otherInstance);

    repeat(vm->stack.top, i) markRValue(runner, &vm->stack.slots[i]);
    repeat(vm->localVarCount, i) markRValue(runner, &vm->localVars[i]);
    repeat(vm->scriptArgCount, i) markRValue(runner, &vm->scriptArgs[i]);

    for (CallFrame* frame = vm->callStack; frame != nullptr; frame = frame->parent) {
        repeat(frame->savedLocalsCount, i) markRValue(runner, &frame->savedLocals[i]);
        repeat(frame->savedScriptArgCount, i) markRValue(runner, &frame->savedScriptArgs[i]);
    }
    for (EnvFrame* frame = vm->envStack; frame != nullptr; frame = frame->parent) {
        markStruct(runner, frame->savedInstance);
        markStruct(runner, frame->savedOtherInstance);
        repeat(arrlen(frame->instanceList), i) markStruct(runner, frame->instanceList[i]);
    }

    if (vm->staticStructs != nullptr) {
        repeat(vm->dataWin->code.count, i) markStruct(runner, vm->staticStructs[i]);
    }
}

static void markDataStructureRoots(Runner* runner) {
    repeat(arrlen(runner->dsMapPool), i) {
        DsMapEntry* map = runner->dsMapPool[i];
        repeat(hmlen(map), j) markRValue(runner, &map[j].value);
    }
    repeat(arrlen(runner->dsListPool), i) {
        DsList* list = &runner->dsListPool[i];
        if (!list->freed) repeat(arrlen(list->items), j) markRValue(runner, &list->items[j]);
    }
    repeat(arrlen(runner->dsQueuePool), i) {
        DsQueue* queue = &runner->dsQueuePool[i];
        if (!queue->freed) repeat(arrlen(queue->items), j) markRValue(runner, &queue->items[j]);
    }
    repeat(arrlen(runner->dsStackPool), i) {
        DsStack* stack = &runner->dsStackPool[i];
        if (!stack->freed) repeat(arrlen(stack->items), j) markRValue(runner, &stack->items[j]);
    }
    repeat(arrlen(runner->dsPriorityPool), i) {
        DsPriority* priority = &runner->dsPriorityPool[i];
        if (!priority->freed) repeat(arrlen(priority->items), j) markRValue(runner, &priority->items[j].item);
    }
    repeat(arrlen(runner->dsGridPool), i) {
        DsGrid* grid = &runner->dsGridPool[i];
        if (!grid->freed) repeat(grid->width * grid->height, j) markRValue(runner, &grid->items[j]);
    }
}

static void markRunnerRoots(Runner* runner) {
    repeat(arrlen(runner->instances), i) markInstanceValues(runner, runner->instances[i]);

    if (runner->savedRoomStates != nullptr) {
        repeat(runner->dataWin->room.count, roomIndex) {
            SavedRoomState* state = &runner->savedRoomStates[roomIndex];
            if (!state->initialized) continue;
            repeat(arrlen(state->instances), i) markInstanceValues(runner, state->instances[i]);
        }
    }

    repeat(arrlen(runner->structInstances), i) {
        if (runner->structInstances[i]->pinned) markStruct(runner, runner->structInstances[i]);
    }
    markVMRoots(runner);
    markDataStructureRoots(runner);
}

GMLGCStats GMLGC_collect(Runner* runner) {
    GMLGCStats stats = {0};
    if (runner == nullptr || runner->vmContext == nullptr) return stats;

    stats.arraysBefore = GMLArray_gcTrackedCount();
    stats.structsBefore = arrlenu(runner->structInstances);
    GMLArray_gcResetMarks();
    repeat(arrlen(runner->structInstances), i) {
        runner->structInstances[i]->gcMarked = false;
        runner->structInstances[i]->gcGarbage = false;
    }

    markRunnerRoots(runner);

    GMLArray** deadArrays = nullptr;
    repeat(GMLArray_gcTrackedCount(), i) {
        GMLArray* array = GMLArray_gcTrackedAt(i);
        if (!array->gcMarked) {
            array->gcGarbage = true;
            arrput(deadArrays, array);
        }
    }

    Instance** deadStructs = nullptr;
    repeat(arrlen(runner->structInstances), i) {
        Instance* instance = runner->structInstances[i];
        if (!instance->gcMarked && !instance->pinned) {
            instance->gcGarbage = true;
            arrput(deadStructs, instance);
        }
    }

    // Detach every garbage node before releasing graph edges. Reference-count
    // decrements among garbage nodes are suppressed by gcGarbage.
    repeat(arrlen(deadArrays), i) GMLArray_gcUnregister(deadArrays[i]);

    int32_t writeIndex = 0;
    repeat(arrlen(runner->structInstances), i) {
        Instance* instance = runner->structInstances[i];
        if (instance->gcGarbage) {
            (void) hmdel(runner->instancesById, instance->instanceId);
            instance->structRegistryIndex = -1;
        } else {
            runner->structInstances[writeIndex] = instance;
            instance->structRegistryIndex = writeIndex++;
        }
    }
    arrsetlen(runner->structInstances, writeIndex);

    // Disconnect every edge while every garbage shell is still allocated.
    // This is essential for cycles: clearing A may decrement B and vice versa.
    repeat(arrlen(deadStructs), i) Instance_freeContents(deadStructs[i]);
    repeat(arrlen(deadArrays), i) GMLArray_gcClearGarbage(deadArrays[i]);
    repeat(arrlen(deadArrays), i) GMLArray_gcFreeGarbage(deadArrays[i]);
    repeat(arrlen(deadStructs), i) free(deadStructs[i]);

    stats.arraysCollected = arrlenu(deadArrays);
    stats.structsCollected = arrlenu(deadStructs);
    arrfree(deadArrays);
    arrfree(deadStructs);
    return stats;
}
