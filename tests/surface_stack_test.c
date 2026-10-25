#include "runner.h"
#include <assert.h>
#include <stdio.h>

static int currentTarget = 0, flushes = 0;
static void flush(Renderer* renderer) { (void)renderer; ++flushes; }
static bool setTarget(Renderer* renderer, int32_t id, bool implicit) {
    (void)renderer; (void)implicit;
    if (id < 0 || id > 3) return false;
    currentTarget = id;
    return true;
}

int main(void) {
    Runner runner = {0};
    RendererVtable vtable = {0};
    Renderer renderer = {0};
    vtable.flush = flush;
    vtable.setRenderTarget = setTarget;
    renderer.vtable = &vtable;
    runner.renderer = &renderer;
    runner.applicationSurfaceId = 0;
    for (int i = 0; i < MAX_SURFACES; ++i) runner.surfaceStack[i] = -1;
    assert(!Runner_surfaceResetTarget(&runner));
    assert(Runner_surfaceSetTarget(&runner, 1));
    assert(!Runner_surfaceSetTarget(&runner, 99));
    assert(currentTarget == 1 && Runner_surfaceGetTarget(&runner) == 1);
    assert(Runner_surfaceSetTarget(&runner, 2));
    assert(Runner_surfaceResetTarget(&runner));
    assert(currentTarget == 1 && Runner_surfaceGetTarget(&runner) == 1);
    assert(Runner_surfaceResetTarget(&runner));
    assert(currentTarget == 0 && Runner_surfaceGetTarget(&runner) == 0);
    for (int i = 0; i < MAX_SURFACES; ++i) assert(Runner_surfaceSetTarget(&runner, 3));
    int before = flushes;
    assert(!Runner_surfaceSetTarget(&runner, 2));
    assert(currentTarget == 3 && flushes == before);
    for (int i = 0; i < MAX_SURFACES; ++i) assert(Runner_surfaceResetTarget(&runner));
    assert(currentTarget == 0 && !Runner_surfaceResetTarget(&runner));
    puts("PASS: failed target leaves stack unchanged; nesting, restoration and capacity");
    return 0;
}
