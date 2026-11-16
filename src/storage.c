#include "internal.h"
#define STORE_HEADER 12u
static uint16_t crc_byte(uint16_t crc, uint8_t byte) {
    unsigned i;
    crc ^= (uint16_t)((uint16_t)byte << 8);
    for (i = 0; i < 8; ++i)
        crc = (uint16_t)(((uint32_t)(crc & 0x7fffu) << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    return crc;
}
static int poll(const SBPlatform *p) {
    return p->poll && p->poll(p->user);
}
SBError sb_save(const SBPlatform *p, SBSource source) {
    uint8_t header[STORE_HEADER] = {'S', 'B', '1', '0', 1, 0, 0, 0, 0, 0, 0, 0}, byte;
    uint16_t crc = 0xffffu, i, length;
    if (!p || !p->nvwrite || !source.read || p->nvsize < STORE_HEADER ||
        source.length > p->nvsize - STORE_HEADER)
        return SB_STORAGE;
    length = (uint16_t)source.length;
    /* Invalidate first; commit marker is written only after the CRC/header. */
    byte = 0;
    if (p->nvwrite(p->user, 11, &byte, 1))
        return SB_IO;
    for (i = 0; i < length; ++i) {
        int ch;
        if (poll(p))
            return SB_BREAK;
        ch = source.read(source.user, i);
        if (ch < 0 || ch > 255)
            return SB_IO;
        byte = (uint8_t)ch;
        crc = crc_byte(crc, byte);
        if (p->nvwrite(p->user, (uint16_t)(STORE_HEADER + i), &byte, 1))
            return SB_IO;
    }
    put16(header + 6, length);
    put16(header + 8, crc);
    if (p->nvwrite(p->user, 0, header, 11))
        return SB_IO;
    byte = 0xa5;
    return p->nvwrite(p->user, 11, &byte, 1) ? SB_IO : SB_OK;
}
SBError sb_load(const SBPlatform *p, uint8_t *buffer, uint16_t capacity, uint16_t *length) {
    uint8_t header[STORE_HEADER], byte;
    uint16_t n, i, crc = 0xffffu;
    *length = 0;
    if (!p || !p->nvread || p->nvsize < STORE_HEADER)
        return SB_STORAGE;
    if (p->nvread(p->user, 0, header, STORE_HEADER))
        return SB_IO;
    n = get16(header + 6);
    if (memcmp(header, "SB10", 4) || header[4] != 1 || header[5] || header[10] ||
        header[11] != 0xa5 || n > capacity || n > p->nvsize - STORE_HEADER)
        return SB_STORAGE;
    /* Validate before modifying the caller's editable source. */
    for (i = 0; i < n; ++i) {
        if (poll(p))
            return SB_BREAK;
        if (p->nvread(p->user, (uint16_t)(STORE_HEADER + i), &byte, 1))
            return SB_IO;
        crc = crc_byte(crc, byte);
    }
    if (crc != get16(header + 8))
        return SB_STORAGE;
    for (i = 0; i < n; ++i)
        if (p->nvread(p->user, (uint16_t)(STORE_HEADER + i), buffer + i, 1))
            return SB_IO;
    *length = n;
    return SB_OK;
}
