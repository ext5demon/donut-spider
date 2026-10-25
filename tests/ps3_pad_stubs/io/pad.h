// Minimal host mock of the PSL1GHT pad API. Production builds use the SDK header.
#ifndef TEST_PS3_PAD_H
#define TEST_PS3_PAD_H
#include <stdint.h>
#define PAD_BUTTON_OFFSET_DIGITAL1 2
#define PAD_BUTTON_OFFSET_DIGITAL2 3
#define PAD_BUTTON_OFFSET_ANALOG_LEFT_X 6
#define PAD_BUTTON_OFFSET_ANALOG_LEFT_Y 7
#define PAD_CTRL_LEFT (1<<7)
#define PAD_CTRL_DOWN (1<<6)
#define PAD_CTRL_RIGHT (1<<5)
#define PAD_CTRL_UP (1<<4)
#define PAD_CTRL_START (1<<3)
#define PAD_CTRL_SELECT (1<<0)
#define PAD_CTRL_SQUARE (1<<7)
#define PAD_CTRL_CROSS (1<<6)
#define PAD_CTRL_CIRCLE (1<<5)
#define PAD_CTRL_TRIANGLE (1<<4)
#define PAD_CTRL_R1 (1<<3)
#define PAD_CTRL_L1 (1<<2)
#define PAD_CTRL_L2 (1<<0)
#define PAD_ERROR_ALREADY_INITIALIZED 0x80121103u
#define PAD_ERROR_NOT_INITIALIZED 0x80121104u
typedef struct { uint8_t status[127]; } padInfo;
typedef struct { int32_t len;uint16_t button[64]; } padData;
int32_t ioPadInit(uint32_t count);
int32_t ioPadGetInfo(padInfo* info);
int32_t ioPadGetData(uint32_t port,padData* data);
#endif
