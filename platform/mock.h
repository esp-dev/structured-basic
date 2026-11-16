#ifndef SB_MOCK_H
#define SB_MOCK_H
#include "sbasic.h"
typedef struct {
    uint8_t eeprom[4096], direction[32], pins[32];
    uint32_t millis;
    int32_t fail_after;
} SBMock;
void sb_mock_init(SBMock *mock);
int sb_mock_read(void *user, uint16_t address, uint8_t *data, uint16_t length);
int sb_mock_write(void *user, uint16_t address, const uint8_t *data, uint16_t length);
int sb_mock_device(void *user, uint8_t operation, int32_t a, int32_t b, int32_t *result);
#endif
