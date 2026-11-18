#include "internal.h"

static uint16_t width(SBasic *b, uint16_t p) {
    uint8_t op = b->pool[p];
    uint16_t n = 1;
    switch (op) {
    case BC_LINE:
    case BC_JUMP:
    case BC_JZ:
    case BC_JNZ:
    case BC_ARRAY_GET:
    case BC_ARRAY_SET:
    case BC_DIM:
    case BC_INPUT:
    case BC_BUILTIN:
    case BC_NATIVE:
        n = 3;
        break;
    case BC_CONST:
    case BC_FCONST:
    case BC_FOR:
        n = 5;
        break;
    case BC_LOAD_G:
    case BC_STORE_G:
    case BC_LOAD_L:
    case BC_STORE_L:
        n = 2;
        break;
    case BC_CALL:
    case BC_EXIT:
        n = 4;
        break;
    case BC_SCONST:
        if (b->program_end - p < 3)
            return 0;
        n = get16(b->pool + p + 1);
        if (n > SB_STRING_MAX)
            return 0;
        n = (uint16_t)(n + 3u);
        break;
    default:
        if (op > BC_NATIVE)
            return 0;
        break;
    }
    return n <= b->program_end - p ? n : 0;
}
static void mark(uint8_t *map, uint16_t p) {
    map[p / 8u] |= (uint8_t)(1u << (p & 7u));
}
static int marked(const uint8_t *map, uint16_t p, uint16_t end) {
    return p < end && (map[p / 8u] & (uint8_t)(1u << (p & 7u))) != 0;
}
SBError sb_validate(SBasic *b) {
    uint8_t map[(SB_POOL_SIZE + 7u) / 8u];
    uint16_t p, n, q, i, count;
    uint8_t *s;
    b->error = SB_OK;
    b->line = b->error_line = 0;
    if (!b->compiled)
        return sb_fail(b, SB_NOT_COMPILED);
    if (b->program_end > SB_POOL_SIZE || b->code_start != b->symbols_end ||
        b->program_end <= b->code_start || b->globals > SB_SLOT_MAX ||
        b->globals > (SB_POOL_SIZE - b->program_end) / 4u)
        return sb_fail(b, SB_BYTECODE);
    memset(map, 0, sizeof(map));
    for (p = 0; p < b->symbols_end; p = (uint16_t)(p + n)) {
        if (b->symbols_end - p < SYMBOL_HEADER + 1u)
            return sb_fail(b, SB_BYTECODE);
        s = b->pool + p;
        n = (uint16_t)(SYMBOL_HEADER + s[1] + 1u);
        if (!s[1] || s[1] > SB_NAME_MAX || n > b->symbols_end - p || s[n - 1u] ||
            s[0] < SY_GLOBAL || s[0] > SY_LOCAL || (s[7] & TYPE_MASK) > SB_STRING ||
            (s[7] & ~(TYPE_MASK | FLAG_ARRAY | FLAG_CONST | FLAG_SUB | FLAG_DECLARED)))
            return sb_fail(b, SB_BYTECODE);
        for (i = 0; i < s[1]; ++i)
            if (!s[SYMBOL_HEADER + i])
                return sb_fail(b, SB_BYTECODE);
        mark(map, p);
    }
    for (p = b->code_start; p < b->program_end; p = (uint16_t)(p + n)) {
        n = width(b, p);
        if (!n)
            return sb_fail(b, SB_BYTECODE);
        mark(map, p);
    }
    for (p = 0; p < b->symbols_end; p = (uint16_t)(p + SYMBOL_HEADER + s[1] + 1u)) {
        s = b->pool + p;
        if (s[0] == SY_FUNCTION) {
            q = get16(s + 8);
            if (get16(s + 2) != NONE || s[6] < s[5] || q < b->code_start ||
                !marked(map, q, b->program_end))
                return sb_fail(b, SB_BYTECODE);
#if SB_SLOT_MAX < 255u
            if (s[6] > SB_SLOT_MAX)
                return sb_fail(b, SB_BYTECODE);
#endif
            for (i = 0; i < s[6]; ++i)
                if (sb_slot_symbol(b, p, (uint8_t)i) == NONE)
                    return sb_fail(b, SB_BYTECODE);
        } else if (s[0] == SY_GLOBAL) {
            if (get16(s + 2) != NONE || s[4] >= b->globals)
                return sb_fail(b, SB_BYTECODE);
        } else {
            q = get16(s + 2);
            if (q >= b->symbols_end || !marked(map, q, b->symbols_end) ||
                b->pool[q] != SY_FUNCTION || s[4] >= b->pool[q + 6])
                return sb_fail(b, SB_BYTECODE);
            if ((s[7] & FLAG_ARRAY) && s[4] < b->pool[q + 5])
                return sb_fail(b, SB_BYTECODE);
        }
    }
    for (i = 0; i < b->globals; ++i)
        if (sb_slot_symbol(b, NONE, (uint8_t)i) == NONE)
            return sb_fail(b, SB_BYTECODE);
    for (p = b->code_start; p < b->program_end; p = (uint16_t)(p + n)) {
        uint8_t op = b->pool[p];
        n = width(b, p);
        if (op == BC_JUMP || op == BC_JZ || op == BC_JNZ || op == BC_FOR || op == BC_EXIT) {
            q = get16(b->pool + p + (op == BC_FOR ? 3u : op == BC_EXIT ? 2u : 1u));
            if (q < b->code_start || !marked(map, q, b->program_end))
                return sb_fail(b, SB_BYTECODE);
        }
        if (op == BC_CALL || op == BC_ARRAY_GET || op == BC_ARRAY_SET || op == BC_DIM ||
            op == BC_INPUT || op == BC_FOR) {
            q = get16(b->pool + p + 1);
            if (!marked(map, q, b->symbols_end))
                return sb_fail(b, SB_BYTECODE);
            if (op == BC_CALL) {
                if (b->pool[q] != SY_FUNCTION || b->pool[p + 3] != b->pool[q + 5])
                    return sb_fail(b, SB_BYTECODE);
            } else if (b->pool[q] != SY_GLOBAL && b->pool[q] != SY_LOCAL)
                return sb_fail(b, SB_BYTECODE);
        }
        if ((op == BC_LOAD_G || op == BC_STORE_G) && b->pool[p + 1] >= b->globals)
            return sb_fail(b, SB_BYTECODE);
        if (op == BC_BUILTIN &&
            (!sb_builtin_arity(b->pool[p + 1], b->pool[p + 2]) || b->pool[p + 2] > 3))
            return sb_fail(b, SB_BYTECODE);
        if (op == BC_NATIVE && !sb_native_arity(b->natives, b->pool[p + 1], b->pool[p + 2]))
            return sb_fail(b, SB_BYTECODE);
        if (op == BC_EXIT && b->pool[p + 1] > SB_BLOCK_MAX)
            return sb_fail(b, SB_BYTECODE);
        if (op == BC_FCONST) {
            Value a = sb_value_get(b->pool + p + 1, SB_SINGLE);
            if (!isfinite(a.as.f))
                return sb_fail(b, SB_BYTECODE);
        }
    }
    /* Duplicate slots could otherwise make one cell have inconsistent types. */
    for (p = 0; p < b->symbols_end; p = (uint16_t)(p + SYMBOL_HEADER + b->pool[p + 1] + 1u)) {
        s = b->pool + p;
        if (s[0] == SY_FUNCTION)
            continue;
        count = 0;
        for (q = 0; q < b->symbols_end; q = (uint16_t)(q + SYMBOL_HEADER + b->pool[q + 1] + 1u))
            if (b->pool[q] == s[0] && get16(b->pool + q + 2) == get16(s + 2) &&
                b->pool[q + 4] == s[4])
                ++count;
        if (count != 1)
            return sb_fail(b, SB_BYTECODE);
    }
    return SB_OK;
}
