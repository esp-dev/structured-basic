#ifndef SB_TERMINAL_H
#define SB_TERMINAL_H
#include "sbasic.h"
#ifndef SB_TERMINAL_LINE_MAX
#define SB_TERMINAL_LINE_MAX 384u
#endif
typedef struct {
    SBasic *basic;
    const SBPlatform *platform;
    uint8_t *source;
    uint32_t capacity, length, steps, seed;
    int (*file_load)(void *user, const char *name, uint8_t *data, uint32_t capacity,
                     uint32_t *length);
    int (*file_save)(void *user, const char *name, const uint8_t *data, uint32_t length);
} SBTerminal;
int sb_terminal(SBTerminal *terminal);
void sb_diagnostic(const SBPlatform *platform, const SBasic *basic, SBError error);
#endif
