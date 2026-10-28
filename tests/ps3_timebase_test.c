#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static uint64_t testTicks;
uint64_t sysGetTimebaseFrequency(void) { return 79800000; }
#define PLATFORM_PS3
#define __builtin_ppc_get_timebase() testTicks
#include "../src/gettime.c"
int main(void) {
    testTicks=0;assert(nowNanos()==0);
    testTicks=39900;assert(nowNanos()==500000); // sub-millisecond precision
    testTicks=79800000;assert(nowNanos()==1000000000ULL);
    uint64_t start=nowNanos();testTicks+=798000;
    assert((nowNanos()-start)/1000000ULL==10); // current_time in milliseconds
    assert((nowNanos()-start)/1000ULL==10000); // get_timer in microseconds
    testTicks=79800000ULL*60*60*24*400+39900;
    assert(nowNanos()==34560000000500000ULL); // no testTicks * 1e9 overflow
    puts("PASS: PS3 clock preserves nanoseconds, GML milliseconds/microseconds and long uptime");
    return 0;
}
