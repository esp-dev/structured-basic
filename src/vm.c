#include "internal.h"

void sb_init(SBasic *b) { memset(b, 0, sizeof(*b)); }

const char *sb_error_message(SBError e) {
    switch (e) {
    case SB_OK: return "Success";
    case SB_SYNTAX: return "Invalid syntax or block structure";
    case SB_NAME: return "Identifier exceeds 31 characters";
    case SB_NUMBER: return "Integer literal out of range";
    case SB_UNKNOWN: return "Undeclared variable or function";
    case SB_DUPLICATE: return "Duplicate declaration";
    case SB_ARITY: return "Wrong number of arguments";
    case SB_MEMORY: return "BASIC memory pool exhausted";
    case SB_COMPLEXITY: return "Compiler nesting or symbol limit exceeded";
    case SB_STACK: return "VM call or operand stack limit exceeded";
    case SB_ZERO_DIVISION: return "Division by zero";
    case SB_OVERFLOW: return "Integer arithmetic overflow";
    case SB_BYTECODE: return "Invalid bytecode";
    case SB_STEP_LIMIT: return "Instruction budget exhausted";
    case SB_OUTPUT: return "Output callback failed";
    default: return "No compiled program";
    }
}

typedef struct {
    SBasic *b;
    uint16_t pc, top, floor, frame, depth, eval, bottom;
} VM;

static int need(VM *v, uint16_t n) {
    if (v->pc < v->b->code_start || v->pc > v->b->program_end ||
        n > v->b->program_end - v->pc) {
        sb_fail(v->b, SB_BYTECODE); return 0;
    }
    return 1;
}
static int room(VM *v, uint16_t n) {
    if (v->top < v->bottom || n > v->top - v->bottom) {
        sb_fail(v->b, SB_MEMORY); return 0;
    }
    return 1;
}
static void peaks(VM *v) {
    uint16_t used = (uint16_t)(SB_POOL_SIZE - v->top + v->b->globals * 4u);
    if (used > v->b->peak_runtime) v->b->peak_runtime = used;
    if (v->depth > v->b->peak_calls) v->b->peak_calls = v->depth;
    if (v->eval > v->b->peak_eval) v->b->peak_eval = v->eval;
}
static int push(VM *v, int32_t n) {
    if (v->eval == SB_EVAL_MAX) { sb_fail(v->b, SB_STACK); return 0; }
    if (!room(v, 4)) return 0;
    v->top -= 4; put32(v->b->pool + v->top, n); ++v->eval;
    peaks(v); return 1;
}
static int pop(VM *v, int32_t *n) {
    if (!v->eval || v->top + 4u > v->floor) {
        sb_fail(v->b, SB_BYTECODE); return 0;
    }
    *n = get32(v->b->pool + v->top); v->top += 4; --v->eval;
    return 1;
}
static int jump(VM *v, uint16_t target) {
    if (target < v->b->code_start || target >= v->b->program_end) {
        sb_fail(v->b, SB_BYTECODE); return 0;
    }
    v->pc = target; return 1;
}

/* No signed operation is evaluated before proving its result representable.
 * Division/remainder also handle INT32_MIN / -1 before the C operator. */
static SBError arithmetic(uint8_t op, int32_t a, int32_t b, int32_t *r) {
    switch (op) {
    case BC_ADD:
        if ((b > 0 && a > INT32_MAX - b) ||
            (b < 0 && a < INT32_MIN - b)) return SB_OVERFLOW;
        *r = a + b; break;
    case BC_SUB:
        if ((b < 0 && a > INT32_MAX + b) ||
            (b > 0 && a < INT32_MIN + b)) return SB_OVERFLOW;
        *r = a - b; break;
    case BC_MUL:
        if (a > 0 ? (b > 0 ? a > INT32_MAX / b : b < INT32_MIN / a) :
            (a < 0 ? (b > 0 ? a < INT32_MIN / b :
                      (b < 0 && a < INT32_MAX / b)) : 0)) return SB_OVERFLOW;
        *r = a * b; break;
    case BC_DIV: case BC_MOD:
        if (!b) return SB_ZERO_DIVISION;
        if (a == INT32_MIN && b == -1) return SB_OVERFLOW;
        *r = op == BC_DIV ? a / b : a % b; break;
    case BC_EQ: *r = a == b; break;
    case BC_NE: *r = a != b; break;
    case BC_LT: *r = a < b; break;
    case BC_LE: *r = a <= b; break;
    case BC_GT: *r = a > b; break;
    case BC_GE: *r = a >= b; break;
    default: return SB_BYTECODE;
    }
    return SB_OK;
}

SBError sb_run(SBasic *b, SBPrint print, void *user, uint32_t steps) {
    VM v;
    uint8_t op, slot, argc;
    uint16_t operand, size, base, next, saved_eval, saved_floor, locals;
    int32_t a, c, result;
    SBError e;
    b->error = SB_OK; b->line = b->error_line = 0;
    b->peak_runtime = b->peak_calls = b->peak_eval = 0;
    if (!b->compiled) return sb_fail(b, SB_NOT_COMPILED);
    if (b->program_end > SB_POOL_SIZE || b->code_start > b->program_end ||
        b->globals > (SB_POOL_SIZE - b->program_end) / 4u)
        return sb_fail(b, SB_BYTECODE);
    v.b = b; v.pc = b->code_start; v.top = v.floor = SB_POOL_SIZE;
    v.bottom = (uint16_t)(b->program_end + b->globals * 4u);
    v.frame = NONE; v.depth = v.eval = 0;
    memset(b->pool + b->program_end, 0, SB_POOL_SIZE - b->program_end);
    peaks(&v);
    while (b->error == SB_OK) {
        if (!steps--) { sb_fail(b, SB_STEP_LIMIT); break; }
        if (!need(&v, 1)) break;
        op = b->pool[v.pc++];
        switch (op) {
        case BC_HALT: return SB_OK;
        case BC_LINE:
            if (!need(&v, 2)) break;
            b->line = get16(b->pool + v.pc); v.pc += 2; break;
        case BC_CONST:
            if (!need(&v, 4)) break;
            a = get32(b->pool + v.pc); v.pc += 4; push(&v, a); break;
        case BC_LOAD_G: case BC_STORE_G: case BC_LOAD_L: case BC_STORE_L:
            if (!need(&v, 1)) break;
            slot = b->pool[v.pc++];
            if (op == BC_LOAD_G || op == BC_STORE_G) {
                if (slot >= b->globals) { sb_fail(b, SB_BYTECODE); break; }
                operand = (uint16_t)(b->program_end + (uint16_t)slot * 4u);
            } else {
                if (v.frame == NONE || slot >= get16(b->pool + v.frame + 8)) {
                    sb_fail(b, SB_BYTECODE); break;
                }
                operand = (uint16_t)(v.frame - ((uint16_t)slot + 1u) * 4u);
            }
            if (op == BC_STORE_G || op == BC_STORE_L) {
                if (pop(&v, &a)) put32(b->pool + operand, a);
            } else push(&v, get32(b->pool + operand));
            break;
        case BC_NEG:
            if (!pop(&v, &a)) break;
            if (a == INT32_MIN) sb_fail(b, SB_OVERFLOW);
            else push(&v, -a);
            break;
        case BC_JUMP: case BC_JZ:
            if (!need(&v, 2)) break;
            operand = get16(b->pool + v.pc); v.pc += 2;
            if (op == BC_JUMP || (pop(&v, &a) && !a)) jump(&v, operand);
            break;
        case BC_CALL:
            if (!need(&v, 3)) break;
            operand = get16(b->pool + v.pc); argc = b->pool[v.pc + 2]; v.pc += 3;
            if (operand >= b->symbols_end || (uint16_t)(b->symbols_end - operand) < SYMBOL_HEADER ||
                b->pool[operand] != SY_FUNCTION) { sb_fail(b, SB_BYTECODE); break; }
            locals = b->pool[operand + 6];
            if (argc != b->pool[operand + 5] || locals < argc) {
                sb_fail(b, SB_ARITY); break;
            }
            if (v.eval < argc) { sb_fail(b, SB_BYTECODE); break; }
            if (v.depth == SB_CALL_MAX) { sb_fail(b, SB_STACK); break; }
            size = (uint16_t)(FRAME_SIZE + (locals - argc) * 4u);
            if (!room(&v, size)) break;
            base = (uint16_t)(v.top + (uint16_t)argc * 4u);
            next = (uint16_t)(base - FRAME_SIZE);
            memmove(b->pool + next - (uint16_t)argc * 4u,
                    b->pool + v.top, (size_t)argc * 4u);
            put16(b->pool + next, v.pc);
            put16(b->pool + next + 2, v.frame);
            put16(b->pool + next + 4, v.floor);
            put16(b->pool + next + 6, (uint16_t)(v.eval - argc));
            put16(b->pool + next + 8, locals);
            put16(b->pool + next + 10, b->line);
            v.top = (uint16_t)(next - locals * 4u);
            memset(b->pool + v.top, 0, (size_t)(locals - argc) * 4u);
            v.frame = next; v.floor = v.top; v.eval = 0; ++v.depth;
            peaks(&v); jump(&v, get16(b->pool + operand + 8)); break;
        case BC_RETURN:
            if (v.frame == NONE || v.eval != 1 || !pop(&v, &a)) {
                sb_fail(b, SB_BYTECODE); break;
            }
            next = get16(b->pool + v.frame);
            operand = get16(b->pool + v.frame + 2);
            saved_floor = get16(b->pool + v.frame + 4);
            saved_eval = get16(b->pool + v.frame + 6);
            b->line = get16(b->pool + v.frame + 10);
            v.top = (uint16_t)(v.frame + FRAME_SIZE); v.frame = operand;
            v.floor = saved_floor; v.eval = saved_eval; --v.depth;
            if (jump(&v, next)) push(&v, a);
            break;
        case BC_PRINT:
            if (!pop(&v, &a)) break;
            if (!print || print(user, a)) sb_fail(b, SB_OUTPUT);
            break;
        default:
            if (op < BC_ADD || op > BC_GE) { sb_fail(b, SB_BYTECODE); break; }
            if (!pop(&v, &c) || !pop(&v, &a)) break;
            e = arithmetic(op, a, c, &result);
            if (e != SB_OK) sb_fail(b, e); else push(&v, result);
            break;
        }
    }
    return b->error;
}
