#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_error.h>
#include <sys/thread.h>

/* SDL2_PSL1GHT's implementation calls sysThreadGetPriority instead of the
 * setter, and uses POSIX nice values. Wrap it at link time so SDL's audio
 * thread can actually preempt gameplay when it requests time-critical work.
 * PSL1GHT uses priorities 0..3071, with lower numbers running first. */
int __wrap_SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority) {
    s32 nativePriority;
    switch (priority) {
        case SDL_THREAD_PRIORITY_LOW: nativePriority = 2000; break;
        case SDL_THREAD_PRIORITY_NORMAL: nativePriority = 1500; break;
        case SDL_THREAD_PRIORITY_HIGH: nativePriority = 750; break;
        case SDL_THREAD_PRIORITY_TIME_CRITICAL: nativePriority = 500; break;
        default: return SDL_SetError("Invalid PS3 thread priority");
    }
    sys_ppu_thread_t thread;
    s32 result = sysThreadGetId(&thread);
    if (result == 0) result = sysThreadSetPriority(thread, nativePriority);
    if (result != 0) return SDL_SetError("PS3 thread priority failed: 0x%x", (unsigned)result);
    return 0;
}
