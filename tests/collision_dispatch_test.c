#undef ENABLE_VM_TRACING
#include "../src/runner.c"
#include <assert.h>

int main(void) {
    DataWin data = {0};
    GameObject objects[3] = {0};
    objects[0].parentId = -1;
    objects[1].parentId = 0;
    objects[2].parentId = -1;
    data.objt.objects = objects; data.objt.count = 3;
    FlattenedCollisionEvent handlers[2] = {
        {.targetObjectIndex = 0, .codeId = 10},
        {.targetObjectIndex = 1, .codeId = 20}
    };
    FlattenedCollisionEventList lists[3] = {0};
    lists[2].events = handlers; lists[2].eventCount = 2;
    Runner runner = {.dataWin = &data, .flattenedCollisionEvents = lists};
    Instance self = {.objectIndex = 2}, other = {.objectIndex = 1};
    // Child handler wins even though the parent appears first in the table.
    assert(mostSpecificCollisionTarget(&runner, &self, &other) == 1);
    lists[2].eventCount = 1;
    assert(mostSpecificCollisionTarget(&runner, &self, &other) == 0);
    lists[2].eventCount = 2; handlers[1].codeId = -1;
    assert(mostSpecificCollisionTarget(&runner, &self, &other) == -1);
    other.objectIndex = 2;
    assert(mostSpecificCollisionTarget(&runner, &self, &other) == -1);
    puts("PASS: one most-specific collision handler, inherited fallback and suppression");
    return 0;
}
