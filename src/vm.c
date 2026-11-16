#include "internal.h"

int sb_room(VM *v, uint16_t n) {
    if (v->heap > v->top || n > v->top - v->heap) {
        sb_fail(v->b, SB_MEMORY);
        return 0;
    }
    return 1;
}
void sb_peaks(VM *v) {
    SB_ASSERT(v->heap <= v->top && v->top <= SB_POOL_SIZE);
    uint16_t n = (uint16_t)(SB_POOL_SIZE - v->top + v->heap - v->b->program_end);
    if (n > v->b->peak_runtime)
        v->b->peak_runtime = n;
    if (v->depth > v->b->peak_calls)
        v->b->peak_calls = v->depth;
    if (v->eval > v->b->peak_eval)
        v->b->peak_eval = v->eval;
}
int sb_poll(VM *v) {
    if (v->platform && v->platform->poll) {
        int n = v->platform->poll(v->platform->user);
        if (n) {
            sb_fail(v->b, n > 0 ? SB_BREAK : SB_IO);
            return 0;
        }
    }
    return v->b->error == SB_OK;
}
int sb_push(VM *v, Value a) {
    if (v->eval == SB_EVAL_MAX)
        sb_fail(v->b, SB_STACK);
    else if (sb_room(v, VALUE_SIZE)) {
        v->top -= VALUE_SIZE;
        v->b->pool[v->top] = a.type;
        sb_value_put(v->b->pool + v->top + 1, a);
        ++v->eval;
        sb_peaks(v);
        return 1;
    }
    sb_release(v, a);
    return 0;
}
int sb_pop(VM *v, Value *a) {
    if (!v->eval || v->top > v->floor || v->floor - v->top < VALUE_SIZE) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    *a = sb_value_get(v->b->pool + v->top + 1, v->b->pool[v->top]);
    if (a->type > SB_STRING || (a->type == SB_SINGLE && !isfinite(a->as.f))) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    v->top += VALUE_SIZE;
    --v->eval;
    return 1;
}
static int jump(VM *v, uint16_t p) {
    if (p < v->b->code_start || p >= v->b->program_end) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    v->pc = p;
    return 1;
}
static int cell(VM *v, uint16_t symbol, uint16_t *address, uint8_t *type) {
    uint8_t *s;
    if (symbol >= v->b->symbols_end) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    s = v->b->pool + symbol;
    *type = (uint8_t)(s[7] & (TYPE_MASK | FLAG_ARRAY));
    if (s[0] == SY_GLOBAL && s[4] < v->b->globals) {
        *address = (uint16_t)(v->b->program_end + (uint16_t)s[4] * 4u);
        return 1;
    }
    if (s[0] == SY_LOCAL && v->frame != NONE && get16(s + 2) == v->function &&
        s[4] < get16(v->b->pool + v->frame + 8)) {
        *address = (uint16_t)(v->frame - ((uint16_t)s[4] + 1u) * 4u);
        return 1;
    }
    sb_fail(v->b, SB_BYTECODE);
    return 0;
}
static int store(VM *v, uint16_t s, Value a) {
    uint16_t address;
    uint8_t type;
    if (!cell(v, s, &address, &type) || !sb_convert(v, &a, type)) {
        sb_release(v, a);
        return 0;
    }
    sb_release(v, sb_value_get(v->b->pool + address, type));
    sb_value_put(v->b->pool + address, a);
    return 1;
}
static int load(VM *v, uint16_t s) {
    uint16_t address;
    uint8_t type;
    Value a;
    if (!cell(v, s, &address, &type))
        return 0;
    a = sb_value_get(v->b->pool + address, type);
    sb_retain(v, a);
    return sb_push(v, a);
}
static int write_bytes(VM *v, const uint8_t *p, uint16_t n) {
    if (!v->platform || !v->platform->write || v->platform->write(v->platform->user, p, n)) {
        sb_fail(v->b, SB_OUTPUT);
        return 0;
    }
    return 1;
}
static void print_value(VM *v, Value a, int newline) {
    char text[48];
    int n;
    if (v->legacy && newline && a.type == SB_INTEGER) {
        if (v->legacy(v->legacy_user, a.as.i))
            sb_fail(v->b, SB_OUTPUT);
    } else if (a.type == SB_STRING) {
        if (write_bytes(v, sb_bytes(v, a), sb_length(v, a)) && newline)
            write_bytes(v, (const uint8_t *)"\n", 1);
    } else {
        n = sb_format(a, text, sizeof(text));
        if (n < 0 || n >= (int)sizeof(text))
            sb_fail(v->b, SB_CONVERSION);
        else if (write_bytes(v, (uint8_t *)text, (uint16_t)n) && newline)
            write_bytes(v, (const uint8_t *)"\n", 1);
    }
    sb_release(v, a);
}
static void input_value(VM *v, uint16_t s) {
    uint8_t text[SB_INPUT_MAX + 1u], type;
    uint16_t n = 0, address;
    Value a;
    int status;
    if (!cell(v, s, &address, &type))
        return;
    if (type & FLAG_ARRAY) {
        sb_fail(v->b, SB_TYPE);
        return;
    }
    if (!v->platform || !v->platform->input) {
        sb_fail(v->b, SB_IO);
        return;
    }
    status = v->platform->input(v->platform->user, text, SB_INPUT_MAX, &n);
    if (status || n > SB_INPUT_MAX) {
        sb_fail(v->b, status == -2 ? SB_BREAK : SB_IO);
        return;
    }
    text[n] = 0;
    if (type == SB_STRING)
        a = sb_string(v, text, n);
    else {
        a = sb_int(0);
        if (memchr(text, 0, n) || (type == SB_INTEGER ? !sb_integer_text((char *)text, &a.as.i)
                                                      : !sb_parse((char *)text, &a))) {
            sb_fail(v->b, SB_CONVERSION);
            return;
        }
    }
    if (v->b->error == SB_OK)
        store(v, s, a);
}
static int control_push(VM *v, uint8_t kind) {
    uint16_t old = v->floor;
    if (v->eval || v->top != v->floor) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    if (!sb_room(v, CONTROL_SIZE))
        return 0;
    v->top -= CONTROL_SIZE;
    memset(v->b->pool + v->top, 0, CONTROL_SIZE);
    put16(v->b->pool + v->top, v->control);
    v->b->pool[v->top + 2] = kind;
    put16(v->b->pool + v->top + 4, old);
    v->floor = v->control = v->top;
    sb_peaks(v);
    return 1;
}
static int control_pop(VM *v) {
    uint16_t p = v->control;
    if (p == NONE || p != v->top || v->eval || p + CONTROL_SIZE > SB_POOL_SIZE) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    if (v->b->pool[p + 2] == 2)
        sb_release(v, sb_value_get(v->b->pool + p + 7, v->b->pool[p + 6]));
    v->control = get16(v->b->pool + p);
    v->floor = get16(v->b->pool + p + 4);
    v->top = (uint16_t)(p + CONTROL_SIZE);
    if (v->floor != v->top) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    return 1;
}
static void call(VM *v, uint16_t f, uint8_t argc) {
    SBasic *b = v->b;
    uint16_t old = v->top, frame, slots, i, s;
    if (f >= b->symbols_end || b->pool[f] != SY_FUNCTION) {
        sb_fail(b, SB_BYTECODE);
        return;
    }
    slots = b->pool[f + 6];
    if (argc != b->pool[f + 5] || slots < argc) {
        sb_fail(b, SB_ARITY);
        return;
    }
    if (v->eval < argc) {
        sb_fail(b, SB_BYTECODE);
        return;
    }
    if (v->depth == SB_CALL_MAX) {
        sb_fail(b, SB_STACK);
        return;
    }
    if (!sb_room(v, (uint16_t)(FRAME_SIZE + slots * 4u)))
        return;
    frame = (uint16_t)(old - FRAME_SIZE);
    put16(b->pool + frame, v->pc);
    put16(b->pool + frame + 2, v->frame);
    put16(b->pool + frame + 4, v->floor);
    put16(b->pool + frame + 6, (uint16_t)(v->eval - argc));
    put16(b->pool + frame + 8, slots);
    put16(b->pool + frame + 10, b->line);
    put16(b->pool + frame + 12, v->function);
    put16(b->pool + frame + 14, (uint16_t)(old + (uint16_t)argc * VALUE_SIZE));
    put16(b->pool + frame + 16, v->control);
    v->top = (uint16_t)(frame - slots * 4u);
    memset(b->pool + v->top, 0, slots * 4u);
    for (i = 0; i < argc; ++i) {
        uint16_t p = (uint16_t)(old + ((uint16_t)argc - i - 1u) * VALUE_SIZE);
        Value a = sb_value_get(b->pool + p + 1, b->pool[p]);
        s = sb_slot_symbol(b, f, (uint8_t)i);
        if (s == NONE || !sb_convert(v, &a, (uint8_t)(b->pool[s + 7] & TYPE_MASK)))
            return;
        sb_value_put(b->pool + frame - (i + 1u) * 4u, a);
    }
    v->frame = frame;
    v->function = f;
    v->floor = v->top;
    v->control = NONE;
    v->eval = 0;
    ++v->depth;
    sb_peaks(v);
    jump(v, get16(b->pool + f + 8));
}
static void return_value(VM *v, Value a) {
    uint16_t p = v->frame, i, s, target, top, previous, floor, eval, function, control;
    if (p == NONE) {
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    if (!sb_convert(v, &a, (uint8_t)(v->b->pool[v->function + 7] & TYPE_MASK))) {
        sb_release(v, a);
        return;
    }
    while (v->control != NONE)
        if (!control_pop(v))
            return;
    for (i = 0; i < get16(v->b->pool + p + 8); ++i) {
        s = sb_slot_symbol(v->b, v->function, (uint8_t)i);
        if (s == NONE) {
            sb_fail(v->b, SB_BYTECODE);
            return;
        }
        sb_release(v, sb_value_get(v->b->pool + p - (i + 1u) * 4u,
                                   (uint8_t)(v->b->pool[s + 7] & (TYPE_MASK | FLAG_ARRAY))));
    }
    target = get16(v->b->pool + p);
    previous = get16(v->b->pool + p + 2);
    floor = get16(v->b->pool + p + 4);
    eval = get16(v->b->pool + p + 6);
    v->b->line = get16(v->b->pool + p + 10);
    function = get16(v->b->pool + p + 12);
    top = get16(v->b->pool + p + 14);
    control = get16(v->b->pool + p + 16);
    v->frame = previous;
    v->function = function;
    v->top = top;
    v->floor = floor;
    v->eval = eval;
    v->control = control;
    --v->depth;
    if (jump(v, target))
        sb_push(v, a);
}
static void array(VM *v, uint8_t op, uint16_t s) {
    uint16_t address, object, count, offset;
    uint8_t type;
    Value index, a = sb_int(0), owner;
    if (!cell(v, s, &address, &type))
        return;
    if (!(type & FLAG_ARRAY)) {
        sb_fail(v->b, SB_TYPE);
        return;
    }
    if (op == BC_ARRAY_SET && !sb_pop(v, &a))
        return;
    if (!sb_pop(v, &index)) {
        sb_release(v, a);
        return;
    }
    if (index.type != SB_INTEGER) {
        sb_release(v, index);
        sb_release(v, a);
        sb_fail(v->b, SB_TYPE);
        return;
    }
    owner = sb_value_get(v->b->pool + address, type);
    object = owner.as.object;
    if (object && (object < v->bottom || object >= v->heap || v->heap - object < OBJ_HEADER + 4u ||
                   get16(v->b->pool + object) > v->heap - object ||
                   get16(v->b->pool + object) < OBJ_HEADER + 4u || v->b->pool[object + 4] != type ||
                   !get16(v->b->pool + object + 2) ||
                   get16(v->b->pool + object + OBJ_HEADER) >
                       (get16(v->b->pool + object) - OBJ_HEADER - 4u) / 4u)) {
        sb_release(v, a);
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    if (op == BC_DIM) {
        if (index.as.i < 0 || index.as.i > (int32_t)((SB_POOL_SIZE - OBJ_HEADER - 4u) / 4u)) {
            sb_fail(v->b, index.as.i < 0 ? SB_ARGUMENT : SB_MEMORY);
            return;
        }
        count = (uint16_t)index.as.i;
        object = sb_object(v, type, (uint16_t)(4u + count * 4u));
        if (!object)
            return;
        put16(v->b->pool + object + OBJ_HEADER, count);
        v->b->pool[object + OBJ_HEADER + 2] = (uint8_t)(type & TYPE_MASK);
        sb_release(v, owner);
        owner.as.object = object;
        sb_value_put(v->b->pool + address, owner);
        return;
    }
    if (!object || index.as.i < 0 || index.as.i >= get16(v->b->pool + object + OBJ_HEADER)) {
        sb_release(v, a);
        sb_fail(v->b, SB_BOUNDS);
        return;
    }
    offset = (uint16_t)(object + OBJ_HEADER + 4u + (uint16_t)index.as.i * 4u);
    type &= TYPE_MASK;
    if (op == BC_ARRAY_SET) {
        if (!sb_convert(v, &a, type)) {
            sb_release(v, a);
            return;
        }
        sb_release(v, sb_value_get(v->b->pool + offset, type));
        sb_value_put(v->b->pool + offset, a);
    } else {
        a = sb_value_get(v->b->pool + offset, type);
        sb_retain(v, a);
        sb_push(v, a);
    }
}
static void for_begin(VM *v, uint16_t s, uint16_t exit) {
    Value step, limit, start;
    uint16_t address;
    uint8_t type;
    if (!sb_pop(v, &step) || !sb_pop(v, &limit) || !sb_pop(v, &start))
        return;
    if (step.type != SB_INTEGER || limit.type != SB_INTEGER || start.type != SB_INTEGER ||
        !cell(v, s, &address, &type) || type != SB_INTEGER) {
        sb_release(v, step);
        sb_release(v, limit);
        sb_release(v, start);
        sb_fail(v->b, SB_TYPE);
        return;
    }
    if (!step.as.i) {
        sb_fail(v->b, SB_ARGUMENT);
        return;
    }
    sb_value_put(v->b->pool + address, start);
    if (step.as.i > 0 ? start.as.i > limit.as.i : start.as.i < limit.as.i) {
        jump(v, exit);
        return;
    }
    if (!control_push(v, 1))
        return;
    put16(v->b->pool + v->control + 6, s);
    put32(v->b->pool + v->control + 8, limit.as.i);
    put32(v->b->pool + v->control + 12, step.as.i);
    put16(v->b->pool + v->control + 16, v->pc);
    put16(v->b->pool + v->control + 18, exit);
}
static void for_next(VM *v) {
    uint16_t p = v->control, address;
    uint8_t type;
    int32_t current, limit, step, next;
    if (p == NONE || v->b->pool[p + 2] != 1) {
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    if (!cell(v, get16(v->b->pool + p + 6), &address, &type))
        return;
    current = get32(v->b->pool + address);
    limit = get32(v->b->pool + p + 8);
    step = get32(v->b->pool + p + 12);
    if ((step > 0 && current > INT32_MAX - step) || (step < 0 && current < INT32_MIN - step)) {
        control_pop(v);
        return;
    }
    next = current + step;
    if (step > 0 ? next > limit : next < limit) {
        control_pop(v);
        return;
    }
    put32(v->b->pool + address, next);
    jump(v, get16(v->b->pool + p + 16));
}
static SBError execute(SBasic *b, const SBPlatform *platform, SBPrint legacy, void *user,
                       uint32_t steps, uint32_t seed) {
    VM v;
    uint8_t op, slot, argc;
    uint16_t operand, target, i;
    Value a, c, r, args[3];
    SBError e;
    b->error = SB_OK;
    b->line = b->error_line = 0;
    b->peak_runtime = b->peak_calls = b->peak_eval = 0;
    if (!b->compiled)
        return sb_fail(b, SB_NOT_COMPILED);
    if (sb_validate(b) != SB_OK)
        return b->error;
    memset(&v, 0, sizeof(v));
    v.b = b;
    v.platform = platform;
    v.legacy = legacy;
    v.legacy_user = user;
    v.pc = b->code_start;
    v.top = v.floor = SB_POOL_SIZE;
    v.bottom = v.heap = (uint16_t)(b->program_end + b->globals * 4u);
    v.frame = v.function = v.control = NONE;
    v.random = seed ? seed : UINT32_C(0x6d2b79f5);
    memset(b->pool + b->program_end, 0, SB_POOL_SIZE - b->program_end);
    sb_peaks(&v);
    while (b->error == SB_OK) {
        if (!steps) {
            sb_fail(b, SB_STEP_LIMIT);
            break;
        }
        --steps;
        if (!sb_poll(&v))
            break;
        if (v.pc < b->code_start || v.pc >= b->program_end) {
            sb_fail(b, SB_BYTECODE);
            break;
        }
        op = b->pool[v.pc++];
        switch (op) {
        case BC_HALT:
            return SB_OK;
        case BC_LINE:
            b->line = get16(b->pool + v.pc);
            v.pc += 2;
            break;
        case BC_CONST:
            a = sb_int(get32(b->pool + v.pc));
            v.pc += 4;
            sb_push(&v, a);
            break;
        case BC_FCONST:
            a = sb_value_get(b->pool + v.pc, SB_SINGLE);
            v.pc += 4;
            sb_push(&v, a);
            break;
        case BC_SCONST:
            operand = get16(b->pool + v.pc);
            v.pc += 2;
            a = sb_string(&v, b->pool + v.pc, operand);
            v.pc = (uint16_t)(v.pc + operand);
            if (b->error == SB_OK)
                sb_push(&v, a);
            break;
        case BC_LOAD_G:
        case BC_STORE_G:
        case BC_LOAD_L:
        case BC_STORE_L:
            slot = b->pool[v.pc++];
            operand =
                sb_slot_symbol(b, (op == BC_LOAD_G || op == BC_STORE_G) ? NONE : v.function, slot);
            if (operand == NONE) {
                sb_fail(b, SB_BYTECODE);
                break;
            }
            if (op == BC_STORE_G || op == BC_STORE_L) {
                if (sb_pop(&v, &a))
                    store(&v, operand, a);
            } else
                load(&v, operand);
            break;
        case BC_ARRAY_GET:
        case BC_ARRAY_SET:
        case BC_DIM:
            operand = get16(b->pool + v.pc);
            v.pc += 2;
            array(&v, op, operand);
            break;
        case BC_NEG:
        case BC_NOT:
        case BC_BOOL:
            if (!sb_pop(&v, &a))
                break;
            r = sb_int(0);
            if (op == BC_NEG) {
                if (a.type == SB_INTEGER) {
                    if (a.as.i == INT32_MIN)
                        sb_fail(b, SB_OVERFLOW);
                    else
                        r = sb_int(-a.as.i);
                } else if (a.type == SB_SINGLE)
                    r = sb_float(-a.as.f);
                else
                    sb_fail(b, SB_TYPE);
            } else
                r = sb_int(op == BC_NOT ? !sb_truth(&v, a) : sb_truth(&v, a));
            sb_release(&v, a);
            if (b->error == SB_OK)
                sb_push(&v, r);
            break;
        case BC_JUMP:
        case BC_JZ:
        case BC_JNZ:
            operand = get16(b->pool + v.pc);
            v.pc += 2;
            if (op == BC_JUMP)
                jump(&v, operand);
            else if (sb_pop(&v, &a)) {
                int truth = sb_truth(&v, a);
                sb_release(&v, a);
                if (b->error == SB_OK && (op == BC_JNZ ? truth : !truth))
                    jump(&v, operand);
            }
            break;
        case BC_CALL:
            operand = get16(b->pool + v.pc);
            argc = b->pool[v.pc + 2];
            v.pc += 3;
            call(&v, operand, argc);
            break;
        case BC_RETURN:
            if (v.frame == NONE || v.eval != 1) {
                sb_fail(b, SB_BYTECODE);
                break;
            }
            if (sb_pop(&v, &a))
                return_value(&v, a);
            break;
        case BC_PRINT:
        case BC_WRITE:
            if (sb_pop(&v, &a))
                print_value(&v, a, op == BC_PRINT);
            break;
        case BC_NEWLINE:
            write_bytes(&v, (const uint8_t *)"\n", 1);
            break;
        case BC_SEPARATOR:
            write_bytes(&v, (const uint8_t *)" ", 1);
            break;
        case BC_DROP:
            if (sb_pop(&v, &a))
                sb_release(&v, a);
            break;
        case BC_DUP:
            if (sb_pop(&v, &a)) {
                sb_retain(&v, a);
                if (sb_push(&v, a))
                    sb_push(&v, a);
            }
            break;
        case BC_BUILTIN:
            slot = b->pool[v.pc++];
            argc = b->pool[v.pc++];
            for (i = 0; i < argc; ++i)
                if (!sb_pop(&v, &args[argc - i - 1u]))
                    break;
            if (i != argc)
                break;
            e = sb_builtin(&v, slot, argc, args, &r);
            for (i = 0; i < argc; ++i)
                sb_release(&v, args[i]);
            if (e != SB_OK)
                sb_fail(b, e);
            else
                sb_push(&v, r);
            break;
        case BC_INPUT:
            operand = get16(b->pool + v.pc);
            v.pc += 2;
            input_value(&v, operand);
            break;
        case BC_FOR:
            operand = get16(b->pool + v.pc);
            target = get16(b->pool + v.pc + 2);
            v.pc += 4;
            for_begin(&v, operand, target);
            break;
        case BC_NEXT:
            for_next(&v);
            break;
        case BC_SELECT:
            if (sb_pop(&v, &a)) {
                if (control_push(&v, 2)) {
                    b->pool[v.control + 6] = a.type;
                    sb_value_put(b->pool + v.control + 7, a);
                } else
                    sb_release(&v, a);
            }
            break;
        case BC_CASE:
            if (v.control == NONE || b->pool[v.control + 2] != 2) {
                sb_fail(b, SB_BYTECODE);
                break;
            }
            if (!sb_pop(&v, &a))
                break;
            c = sb_value_get(b->pool + v.control + 7, b->pool[v.control + 6]);
            e = sb_math(&v, BC_EQ, a, c, &r);
            sb_release(&v, a);
            if (e != SB_OK)
                sb_fail(b, e);
            else
                sb_push(&v, r);
            break;
        case BC_ENDSELECT:
            control_pop(&v);
            break;
        case BC_EXIT:
            slot = b->pool[v.pc++];
            operand = get16(b->pool + v.pc);
            v.pc += 2;
            for (i = 0; i < slot; ++i)
                if (!control_pop(&v))
                    break;
            if (b->error == SB_OK)
                jump(&v, operand);
            break;
        case BC_OR:
        case BC_AND:
            if (!sb_pop(&v, &c) || !sb_pop(&v, &a))
                break;
            r = sb_int(op == BC_OR ? sb_truth(&v, a) || sb_truth(&v, c)
                                   : sb_truth(&v, a) && sb_truth(&v, c));
            sb_release(&v, a);
            sb_release(&v, c);
            if (b->error == SB_OK)
                sb_push(&v, r);
            break;
        default:
            if (op < BC_ADD || op > BC_GE) {
                sb_fail(b, SB_BYTECODE);
                break;
            }
            if (!sb_pop(&v, &c) || !sb_pop(&v, &a))
                break;
            e = sb_math(&v, op, a, c, &r);
            sb_release(&v, a);
            sb_release(&v, c);
            if (e != SB_OK)
                sb_fail(b, e);
            else
                sb_push(&v, r);
            break;
        }
    }
    return b->error;
}
SBError sb_run(SBasic *b, SBPrint print, void *user, uint32_t steps) {
    return execute(b, NULL, print, user, steps, 1);
}
SBError sb_execute(SBasic *b, const SBPlatform *p, uint32_t steps, uint32_t seed) {
    return execute(b, p, NULL, NULL, steps, seed);
}
