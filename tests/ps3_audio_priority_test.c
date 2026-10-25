#include <SDL2/SDL_thread.h>
#include <sys/thread.h>
#include <assert.h>
#include <stdio.h>

int __wrap_SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority);
static int idResult, setResult, setCalls, errors, lastPriority;
int sysThreadGetId(sys_ppu_thread_t* id) { *id = 42; return idResult; }
int sysThreadSetPriority(sys_ppu_thread_t id, s32 priority) {
    assert(id == 42); ++setCalls; lastPriority = priority; return setResult;
}
int SDL_SetError(const char* format, ...) { (void)format; ++errors; return -1; }

int main(void) {
    assert(__wrap_SDL_SYS_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL) == 0);
    int normal = lastPriority;
    assert(__wrap_SDL_SYS_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL) == 0);
    assert(lastPriority >= 0 && lastPriority < normal && lastPriority < 1000);
    assert(setCalls == 2 && errors == 0);
    idResult = -1;
    assert(__wrap_SDL_SYS_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) == -1);
    assert(setCalls == 2 && errors == 1);
    idResult = 0; setResult = -1;
    assert(__wrap_SDL_SYS_SetThreadPriority(SDL_THREAD_PRIORITY_LOW) == -1);
    assert(setCalls == 3 && errors == 2);
    assert(__wrap_SDL_SYS_SetThreadPriority((SDL_ThreadPriority)99) == -1);
    assert(setCalls == 3 && errors == 3);
    puts("PASS: audio priority invokes the setter, preempts gameplay, and propagates syscall errors");
    return 0;
}
