#ifndef SBASIC_H
#define SBASIC_H
#include <stddef.h>
#include <stdint.h>

#ifndef SB_POOL_SIZE
#define SB_POOL_SIZE 8192u
#endif
#ifndef SB_NAME_MAX
#define SB_NAME_MAX 31u
#endif
#ifndef SB_EXPR_MAX
#define SB_EXPR_MAX 64u
#endif
#ifndef SB_BLOCK_MAX
#define SB_BLOCK_MAX 32u
#endif
#ifndef SB_CALL_MAX
#define SB_CALL_MAX 32u
#endif
#ifndef SB_EVAL_MAX
#define SB_EVAL_MAX 128u
#endif
#ifndef SB_STRING_MAX
#define SB_STRING_MAX 255u
#endif
#ifndef SB_INPUT_MAX
#define SB_INPUT_MAX 96u
#endif
#ifndef SB_SLOT_MAX
#define SB_SLOT_MAX 255u
#endif
#ifndef SB_NUMBER_MAX
#define SB_NUMBER_MAX 63u
#endif
#ifndef SB_LINE_LIMIT
#define SB_LINE_LIMIT 65535u
#endif
#ifndef SB_DELAY_MAX
#define SB_DELAY_MAX 60000L
#endif
#define SB_TOKEN_MAX ((SB_STRING_MAX) > (SB_NUMBER_MAX) ? (SB_STRING_MAX) : (SB_NUMBER_MAX))
#if SB_POOL_SIZE > 16384u || SB_POOL_SIZE < 256u || SB_NAME_MAX > 255u || SB_EXPR_MAX > 255u ||    \
    SB_BLOCK_MAX > 255u || SB_SLOT_MAX > 255u
#error Unsupported BASIC limit configuration
#endif

typedef enum {
    SB_OK,
    SB_SYNTAX,
    SB_NAME,
    SB_NUMBER,
    SB_UNKNOWN,
    SB_DUPLICATE,
    SB_ARITY,
    SB_MEMORY,
    SB_COMPLEXITY,
    SB_STACK,
    SB_ZERO_DIVISION,
    SB_OVERFLOW,
    SB_BYTECODE,
    SB_STEP_LIMIT,
    SB_OUTPUT,
    SB_NOT_COMPILED,
    SB_TYPE,
    SB_BOUNDS,
    SB_CONVERSION,
    SB_ARGUMENT,
    SB_BREAK,
    SB_IO,
    SB_STORAGE
} SBError;

/* read() returns an unsigned byte or -1. The source is borrowed only during
 * compilation. Its length and all offsets are bytes, not C string lengths. */
typedef int (*SBRead)(void *user, uint32_t offset);
typedef struct {
    SBRead read;
    void *user;
    uint32_t length;
} SBSource;
/* Return zero on success. Each callback represents one PRINT line. */
typedef int (*SBPrint)(void *user, int32_t value);

typedef enum { SB_INTEGER, SB_SINGLE, SB_STRING } SBType;
enum { SB_EEREAD, SB_EEWRITE, SB_PINMODE, SB_PINREAD, SB_PINWRITE, SB_ADC, SB_DELAY, SB_MILLIS };
typedef struct {
    void *user;
    int (*write)(void *user, const uint8_t *data, uint16_t length);
    int (*input)(void *user, uint8_t *data, uint16_t capacity, uint16_t *length);
    int (*poll)(void *user);
    int (*device)(void *user, uint8_t operation, int32_t a, int32_t b, int32_t *result);
    int (*nvread)(void *user, uint16_t address, uint8_t *data, uint16_t length);
    int (*nvwrite)(void *user, uint16_t address, const uint8_t *data, uint16_t length);
    uint16_t nvsize;
    int (*diagnostic)(void *user, const uint8_t *data, uint16_t length);
} SBPlatform;

typedef struct {
    uint8_t pool[SB_POOL_SIZE];
    uint16_t symbols_end, code_start, program_end, globals;
    uint16_t line, error_line, peak_runtime, peak_calls, peak_eval;
    SBError error;
    uint8_t compiled;
} SBasic;

/* Initialize once; compile invalidates any previous program, including on error.
 * run resets globals, frames and operands on every call, including after errors.
 * Separate instances are independent. A single instance is not reentrant. */
void sb_init(SBasic *basic);
SBError sb_compile(SBasic *basic, SBSource source);
SBError sb_run(SBasic *basic, SBPrint print, void *user, uint32_t steps);
const char *sb_error_message(SBError error);
SBError sb_execute(SBasic *basic, const SBPlatform *platform, uint32_t steps, uint32_t seed);
SBError sb_validate(SBasic *basic);
SBError sb_save(const SBPlatform *platform, SBSource source);
SBError sb_load(const SBPlatform *platform, uint8_t *buffer, uint16_t capacity, uint16_t *length);
#endif
