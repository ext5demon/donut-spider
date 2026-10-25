#ifndef PS3_STICK_H
#define PS3_STICK_H
#include <stdbool.h>
#include <stdint.h>

// A held direction needs a wider release band so tiny stick fluctuations do
// not alternate key-up/key-down while walking, especially on diagonals.
static inline bool PS3Stick_direction(uint8_t value, int sign, bool held) {
    int distance = sign * ((int)value - 128);
    return distance > (held ? 48 : 64);
}
#endif
