#include "ps3_stick.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    bool held=false;
    uint8_t motion[]={193,191,194,189,180,177};
    for(unsigned i=0;i<sizeof(motion);++i) {
        held=PS3Stick_direction(motion[i],1,held);
        assert(held);
        assert(!PS3Stick_direction(motion[i],-1,false));
    }
    assert(!PS3Stick_direction(176,1,held));
    assert(!PS3Stick_direction(192,1,false));
    assert(PS3Stick_direction(63,-1,false));
    assert(PS3Stick_direction(79,-1,true));
    assert(!PS3Stick_direction(80,-1,true));
    assert(!PS3Stick_direction(128,1,true));
    assert(!PS3Stick_direction(128,-1,true));
    puts("PASS: analog direction hysteresis, release, opposite direction and neutral");
}
