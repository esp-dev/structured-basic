#ifndef SB_INTERNAL_H
#define SB_INTERNAL_H
#include "sbasic.h"
#include <limits.h>
#include <string.h>
#define NONE UINT16_MAX
#define SYMBOL_HEADER 10u
#define FRAME_SIZE 12u
enum { SY_GLOBAL = 1, SY_FUNCTION, SY_LOCAL };
enum {
    BC_HALT, BC_LINE, BC_CONST, BC_LOAD_G, BC_STORE_G, BC_LOAD_L,
    BC_STORE_L, BC_ADD, BC_SUB, BC_MUL, BC_DIV, BC_MOD, BC_NEG,
    BC_EQ, BC_NE, BC_LT, BC_LE, BC_GT, BC_GE, BC_JUMP, BC_JZ,
    BC_CALL, BC_RETURN, BC_PRINT
};
static inline uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}
static inline int32_t get32(const uint8_t *p) {
    uint32_t n = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static inline void put32(uint8_t *p, int32_t n) {
    uint32_t u = (uint32_t)n;
    p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24);
}
static inline SBError sb_fail(SBasic *b, SBError e) {
    if (b->error == SB_OK) { b->error = e; b->error_line = b->line; }
    return b->error;
}
#endif
