#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sbasic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned char before[32]; SBasic b; unsigned char after[32]; } Guard;
static Guard guarded;
static SBasic other;
static int32_t values[256];
static unsigned count;
static char large_source[16000];
static int read_byte(void *user, uint32_t offset) { return ((unsigned char *)user)[offset]; }
static SBError compile(SBasic *b, const char *s) {
    SBSource source = { read_byte, (void *)s, 0 };
    source.length = (uint32_t)strlen(s); return sb_compile(b, source);
}
static int output(void *user, int32_t value) {
    (void)user; assert(count < 256); values[count++] = value; return 0;
}
static int failed_output(void *user, int32_t value) { (void)user; (void)value; return 1; }
static void guards(void) {
    unsigned i;
    for (i = 0; i < 32; ++i) {
        assert(guarded.before[i] == 0xA5); assert(guarded.after[i] == 0x5A);
    }
}
int main(void) {
    unsigned i, j;
    char fuzz[384];
    uint32_t rng = UINT32_C(0x12345678);
    SBasic *b = &guarded.b;
    memset(guarded.before, 0xA5, 32); memset(guarded.after, 0x5A, 32);
    sb_init(b);
    assert(sb_run(b, output, NULL, 100) == SB_NOT_COMPILED);
    assert(compile(b, "X=X+1\nPRINT X\n") == SB_OK);
    for (i = 0; i < 100; ++i) {
        count = 0; assert(sb_run(b, output, NULL, 100) == SB_OK);
        assert(count == 1 && values[0] == 1); guards();
    }
    assert(compile(&other, "PRINT 42\n") == SB_OK);
    count = 0; assert(sb_run(&other, output, NULL, 100) == SB_OK && values[0] == 42);
    count = 0; assert(sb_run(b, output, NULL, 100) == SB_OK && values[0] == 1);
    assert(compile(b, "PRINT 7\nPRINT 1/0\n") == SB_OK);
    for (i = 0; i < 20; ++i) {
        count = 0; assert(sb_run(b, output, NULL, 100) == SB_ZERO_DIVISION);
        assert(count == 1 && values[0] == 7 && b->error_line == 2);
    }
    assert(compile(b, "PRINT F() / 0\nFUNCTION F()\nRETURN 1\nEND FUNCTION\n") == SB_OK);
    assert(sb_run(b, output, NULL, 100) == SB_ZERO_DIVISION && b->error_line == 1);
    assert(compile(b, "PRINT 1 +\n") == SB_SYNTAX);
    assert(sb_run(b, output, NULL, 100) == SB_NOT_COMPILED);
    assert(compile(b, "PRINT 1\n") == SB_OK);
    assert(sb_run(b, failed_output, NULL, 100) == SB_OUTPUT);
    assert(sb_run(b, NULL, NULL, 100) == SB_OUTPUT);
    count = 0; assert(sb_run(b, output, NULL, 100) == SB_OK && values[0] == 1);
    assert(compile(b, "WHILE 1\nWEND\n") == SB_OK);
    assert(sb_run(b, output, NULL, 100) == SB_STEP_LIMIT);
    assert(sb_run(b, output, NULL, 0) == SB_STEP_LIMIT);
    assert(compile(b, "PRINT 1\n") == SB_OK);
    b->pool[b->code_start] = 255;
    assert(sb_run(b, output, NULL, 100) == SB_BYTECODE);
    assert(compile(b, "PRINT F(32)\nFUNCTION F(N)\nIF N=1 THEN\nRETURN 1\nEND IF\nRETURN F(N-1)\nEND FUNCTION\n") == SB_OK);
    count = 0; assert(sb_run(b, output, NULL, 10000) == SB_OK);
    assert(values[0] == 1 && b->peak_calls == SB_CALL_MAX);
    assert(compile(b, "PRINT F(33)\nFUNCTION F(N)\nIF N=1 THEN\nRETURN 1\nEND IF\nRETURN F(N-1)\nEND FUNCTION\n") == SB_OK);
    for (i = 0; i < 10; ++i) {
        assert(sb_run(b, output, NULL, 10000) == SB_STACK);
        assert(b->peak_calls == SB_CALL_MAX); guards();
    }
    strcpy(large_source, "FUNCTION F()\nLOCAL ");
    for (i = 0; i < 100; ++i) {
        char name[12]; snprintf(name, sizeof(name), "%sL%u", i ? "," : "", i);
        strcat(large_source, name);
    }
    strcat(large_source, "\nRETURN F()\nEND FUNCTION\nPRINT F()\n");
    assert(compile(b, large_source) == SB_OK);
    for (i = 0; i < 10; ++i) {
        assert(sb_run(b, output, NULL, 10000) == SB_MEMORY);
        assert(b->peak_calls < SB_CALL_MAX);
        assert(b->peak_runtime <= SB_POOL_SIZE - b->program_end); guards();
    }
    large_source[0] = 0;
    for (i = 0; i < 100; ++i) {
        char name[16]; snprintf(name, sizeof(name), "G%u=0\n", i);
        strcat(large_source, name);
    }
    for (i = 0; i < 610; ++i) strcat(large_source, "PRINT 0\n");
    assert(compile(b, large_source) == SB_MEMORY);
    /* Establish that code fits; this error is specifically the data reserve. */
    assert(b->program_end == 7881 && b->globals == 100);
    assert(b->program_end < SB_POOL_SIZE && b->program_end + 4u*b->globals > SB_POOL_SIZE);
    assert(compile(b, "PRINT 42\n") == SB_OK);
    count = 0; assert(sb_run(b, output, NULL, 100) == SB_OK && values[0] == 42);
    assert(compile(b, "PRINT 1\n") == SB_OK);
    b->program_end = (uint16_t)(b->code_start + 1);
    assert(sb_run(b, output, NULL, 100) == SB_BYTECODE);
    /* Deterministic malformed-source stress with instance boundary canaries.
     * No host memory allocator is used by either this harness or the core. */
    for (i = 0; i < 12000; ++i) {
        unsigned n = i % (sizeof(fuzz) - 1);
        for (j = 0; j < n; ++j) {
            static const char alphabet[] = "PRINT LET IF END FUNCTION LOCAL WHILE ()<>=,+-*/'0123456789\n\r\t_";
            rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
            fuzz[j] = alphabet[(rng >> 16) % (sizeof(alphabet) - 1)];
        }
        fuzz[n] = 0;
        if (compile(b, fuzz) == SB_OK) { count = 0; sb_run(b, output, NULL, 1000); }
        guards();
    }
    /* Token-level fuzz reaches the block/expression parser more often than
     * random characters. All accepted programs have a bounded run budget. */
    for (i = 0; i < 6000; ++i) {
        static const char *const tokens[] = {"IF ", "THEN\n", "END IF\n", "ELSE\n", "WHILE ", "WEND\n", "FUNCTION F(", "END FUNCTION\n", "LOCAL X\n", "RETURN ", "PRINT ", "LET X=", "X", "F", "0", "1", "-", "+", "*", "/", "MOD ", "(", ")", ",", "\n", "=", "<=", "ELSEIF "};
        size_t length = 0;
        fuzz[0] = 0;
        for (j = 0; j < 30; ++j) {
            const char *token;
            rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
            token = tokens[(rng >> 16) % (sizeof(tokens)/sizeof(tokens[0]))];
            length += strlen(token); assert(length < sizeof(fuzz)); strcat(fuzz, token);
        }
        if (compile(b, fuzz) == SB_OK) { count = 0; sb_run(b, output, NULL, 100); }
        guards();
    }
    printf("API PASS: reset, recovery, independent contexts, diagnostics, callbacks, bytecode guards, exact call/data limits, 18000 source fuzz cases; sizeof(SBasic)=%u\n", (unsigned)sizeof(SBasic));
    return 0;
}
