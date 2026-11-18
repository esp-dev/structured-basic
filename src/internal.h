#ifndef SB_INTERNAL_H
#define SB_INTERNAL_H
#include "sbasic.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#ifdef SB_DEBUG
#include <assert.h>
#define SB_ASSERT(c) assert(c)
#else
#define SB_ASSERT(c) ((void)0)
#endif
#define NONE UINT16_MAX
#define SYMBOL_HEADER 10u
#define FRAME_SIZE 18u
#define CONTROL_SIZE 20u
#define VALUE_SIZE 5u
#define OBJ_HEADER 6u
#define FLAG_ARRAY 4u
#define FLAG_CONST 8u
#define FLAG_SUB 16u
#define FLAG_DECLARED 32u
#define TYPE_MASK 3u
enum { SY_GLOBAL = 1, SY_FUNCTION, SY_LOCAL };
enum {
    BC_HALT,
    BC_LINE,
    BC_CONST,
    BC_LOAD_G,
    BC_STORE_G,
    BC_LOAD_L,
    BC_STORE_L,
    BC_ADD,
    BC_SUB,
    BC_MUL,
    BC_DIV,
    BC_MOD,
    BC_NEG,
    BC_EQ,
    BC_NE,
    BC_LT,
    BC_LE,
    BC_GT,
    BC_GE,
    BC_JUMP,
    BC_JZ,
    BC_CALL,
    BC_RETURN,
    BC_PRINT,
    BC_FCONST,
    BC_SCONST,
    BC_ARRAY_GET,
    BC_ARRAY_SET,
    BC_DIM,
    BC_BUILTIN,
    BC_DROP,
    BC_WRITE,
    BC_NEWLINE,
    BC_SEPARATOR,
    BC_NOT,
    BC_BOOL,
    BC_AND,
    BC_OR,
    BC_DUP,
    BC_INPUT,
    BC_FOR,
    BC_NEXT,
    BC_SELECT,
    BC_CASE,
    BC_ENDSELECT,
    BC_JNZ,
    BC_EXIT,
    BC_NATIVE
};
enum {
    BI_ABS,
    BI_MIN,
    BI_MAX,
    BI_SGN,
    BI_INT,
    BI_CINT,
    BI_CSNG,
    BI_RND,
    BI_SEED,
    BI_LEN,
    BI_LEFT,
    BI_RIGHT,
    BI_MID,
    BI_CHR,
    BI_ASC,
    BI_STR,
    BI_VAL,
    BI_UCASE,
    BI_LCASE,
    BI_EEREAD,
    BI_EEWRITE,
    BI_PINMODE,
    BI_PINREAD,
    BI_PINWRITE,
    BI_ADC,
    BI_DELAY,
    BI_MILLIS,
    BI_COUNT
};
typedef struct {
    uint8_t type;
    union {
        int32_t i;
        float f;
        uint16_t object;
    } as;
} Value;
typedef struct {
    SBasic *b;
    const SBPlatform *platform;
    SBPrint legacy;
    void *legacy_user;
    uint16_t pc, top, floor, frame, function, depth, eval, bottom, heap;
    uint16_t control;
    uint32_t random;
} VM;
Value sb_int(int32_t n);
Value sb_float(float n);
Value sb_zero(uint8_t type);
Value sb_value_get(const uint8_t *p, uint8_t type);
void sb_value_put(uint8_t *p, Value value);
void sb_retain(VM *v, Value value);
void sb_release(VM *v, Value value);
uint16_t sb_object(VM *v, uint8_t kind, uint16_t payload);
Value sb_string(VM *v, const uint8_t *bytes, uint16_t length);
uint16_t sb_length(VM *v, Value value);
const uint8_t *sb_bytes(VM *v, Value value);
int sb_convert(VM *v, Value *value, uint8_t type);
int sb_truth(VM *v, Value value);
SBError sb_math(VM *v, uint8_t op, Value a, Value b, Value *result);
SBError sb_builtin(VM *v, uint8_t id, uint8_t argc, Value *args, Value *result);
int sb_builtin_id(const char *name);
int sb_builtin_arity(uint8_t id, uint8_t argc);
int sb_native_id(const SBNativeRegistry *registry, const char *name);
int sb_native_arity(const SBNativeRegistry *registry, uint8_t id, uint8_t argc);
void sb_call_native(VM *v, uint8_t id, uint8_t argc);
int sb_format(Value value, char *buffer, size_t capacity);
int sb_parse(const char *text, Value *result);
int sb_integer_text(const char *text, int32_t *result);
uint16_t sb_slot_symbol(SBasic *b, uint16_t function, uint8_t slot);
int sb_room(VM *v, uint16_t bytes);
int sb_poll(VM *v);
int sb_push(VM *v, Value value);
int sb_pop(VM *v, Value *value);
void sb_peaks(VM *v);
static inline uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static inline int32_t get32(const uint8_t *p) {
    uint32_t n =
        (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static inline void put32(uint8_t *p, int32_t n) {
    uint32_t u = (uint32_t)n;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}
static inline SBError sb_fail(SBasic *b, SBError e) {
    if (b->error == SB_OK) {
        b->error = e;
        b->error_line = b->line;
    }
    return b->error;
}
#endif
