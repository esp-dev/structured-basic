#include "mock.h"
#include <string.h>
void sb_mock_init(SBMock *m) {
    memset(m, 0, sizeof(*m));
    memset(m->eeprom, 255, sizeof(m->eeprom));
    m->fail_after = -1;
}
int sb_mock_read(void *user, uint16_t address, uint8_t *data, uint16_t n) {
    SBMock *m = user;
    if (address > sizeof(m->eeprom) || n > sizeof(m->eeprom) - address)
        return -1;
    memcpy(data, m->eeprom + address, n);
    return 0;
}
int sb_mock_write(void *user, uint16_t address, const uint8_t *data, uint16_t n) {
    SBMock *m = user;
    uint16_t i;
    if (address > sizeof(m->eeprom) || n > sizeof(m->eeprom) - address)
        return -1;
    for (i = 0; i < n; ++i) {
        if (!m->fail_after)
            return -1;
        if (m->fail_after > 0)
            --m->fail_after;
        m->eeprom[address + i] = data[i];
    }
    return 0;
}
int sb_mock_device(void *user, uint8_t operation, int32_t a, int32_t b, int32_t *r) {
    SBMock *m = user;
    uint8_t byte;
    *r = 0;
    if (operation == SB_EEREAD || operation == SB_EEWRITE) {
        if (a < 0 || a >= 4096 || (operation == SB_EEWRITE && (b < 0 || b > 255)))
            return -1;
        if (operation == SB_EEREAD) {
            *r = m->eeprom[a];
            return 0;
        }
        byte = (uint8_t)b;
        return sb_mock_write(m, (uint16_t)a, &byte, 1);
    }
    if (operation == SB_ADC) {
        if (a < 0 || a > 7)
            return -1;
        *r = a * 127;
        return 0;
    }
    if (operation == SB_MILLIS) {
        *r = (int32_t)(m->millis & UINT32_C(0x7fffffff));
        return 0;
    }
    if (operation == SB_DELAY) {
        if (a < 0 || a > 60000)
            return -1;
        m->millis = (m->millis + (uint32_t)a) & UINT32_C(0x7fffffff);
        return 0;
    }
    if (a < 0 || a > 31 || a == 24 || a == 25)
        return -1;
    if (operation == SB_PINMODE) {
        if (b < 0 || b > 1)
            return -1;
        m->direction[a] = (uint8_t)b;
    } else if (operation == SB_PINWRITE) {
        if (b < 0 || b > 1 || !m->direction[a])
            return -1;
        m->pins[a] = (uint8_t)b;
    } else if (operation == SB_PINREAD)
        *r = m->pins[a];
    else
        return -1;
    return 0;
}
