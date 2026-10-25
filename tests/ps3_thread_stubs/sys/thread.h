#include <stdint.h>
typedef int32_t s32;
typedef uint64_t sys_ppu_thread_t;
int sysThreadGetId(sys_ppu_thread_t* id);
int sysThreadSetPriority(sys_ppu_thread_t id, s32 priority);
