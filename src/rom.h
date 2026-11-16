#ifndef SB_ROM_H
#define SB_ROM_H
/* Address-space adaptation only. Device I/O remains in the platform layer. */
#ifdef __AVR__
#include <avr/pgmspace.h>
#define SB_ROM PROGMEM
static inline unsigned char sb_rom_byte(const void *p) {
    return pgm_read_byte(p);
}
#else
#define SB_ROM
static inline unsigned char sb_rom_byte(const void *p) {
    return *(const unsigned char *)p;
}
#endif
static inline int sb_rom_equal(const char *a, const char *b) {
    unsigned char ch;
    do {
        ch = sb_rom_byte(b++);
        if ((unsigned char)*a++ != ch)
            return 0;
    } while (ch);
    return 1;
}
#endif
