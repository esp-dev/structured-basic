#ifndef SBASIC_H
#define SBASIC_H
#include <stddef.h>
#include <stdint.h>

#define SB_POOL_SIZE 8192u
#define SB_NAME_MAX 31u
#define SB_EXPR_MAX 64u
#define SB_BLOCK_MAX 32u
#define SB_CALL_MAX 32u
#define SB_EVAL_MAX 128u

typedef enum {
    SB_OK, SB_SYNTAX, SB_NAME, SB_NUMBER, SB_UNKNOWN, SB_DUPLICATE,
    SB_ARITY, SB_MEMORY, SB_COMPLEXITY, SB_STACK, SB_ZERO_DIVISION,
    SB_OVERFLOW, SB_BYTECODE, SB_STEP_LIMIT, SB_OUTPUT, SB_NOT_COMPILED
} SBError;

/* read() returns an unsigned byte or -1. The source is borrowed only during
 * compilation. Its length and all offsets are bytes, not C string lengths. */
typedef int (*SBRead)(void *user, uint32_t offset);
typedef struct { SBRead read; void *user; uint32_t length; } SBSource;
/* Return zero on success. Each callback represents one PRINT line. */
typedef int (*SBPrint)(void *user, int32_t value);

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
#endif
