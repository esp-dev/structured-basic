#include "internal.h"
#include "rom.h"

struct SBNativeCall {
    VM *vm;
    uint8_t argc;
    Value result;
};

static SBNativeFunction callback(const SBNativeEntry *entry) {
#ifdef __AVR__
    SBNativeFunction function;
    memcpy_P(&function, &entry->function, sizeof(function));
    return function;
#else
    return entry->function;
#endif
}

int sb_native_id(const SBNativeRegistry *registry, const char *name) {
    if (registry && registry->entries)
        for (unsigned i = 0; i < registry->count; ++i)
            if (sb_rom_equal(name, registry->entries[i].name))
                return (int)i;
    return -1;
}

int sb_native_arity(const SBNativeRegistry *registry, uint8_t id, uint8_t argc) {
    if (!registry || !registry->entries || id >= registry->count)
        return 0;
#if SB_EVAL_MAX < 255u
    if (argc > SB_EVAL_MAX)
        return 0;
#endif
    const SBNativeEntry *entry = &registry->entries[id];
    return argc >= sb_rom_byte(&entry->min_args) && argc <= sb_rom_byte(&entry->max_args) &&
           callback(entry) != NULL;
}

uint8_t sb_native_argc(const SBNativeCall *call) {
    return call->argc;
}

SBError sb_native_arg(const SBNativeCall *call, uint8_t index, SBNativeValue *value) {
    if (index >= call->argc || !value)
        return SB_ARGUMENT;
    VM *v = call->vm;
    const uint8_t *cell = v->b->pool + v->top + ((unsigned)call->argc - index - 1u) * VALUE_SIZE;
    Value arg = sb_value_get(cell + 1, *cell);
    value->type = (SBType)arg.type;
    switch (arg.type) {
    case SB_INTEGER:
        value->as.integer = arg.as.i;
        return SB_OK;
    case SB_SINGLE:
        value->as.single = arg.as.f;
        return isfinite(arg.as.f) ? SB_OK : sb_fail(v->b, SB_BYTECODE);
    case SB_STRING:
        value->as.string.length = sb_length(v, arg);
        if (v->b->error != SB_OK)
            return v->b->error;
        value->as.string.data = sb_bytes(v, arg);
        return SB_OK;
    default:
        return sb_fail(v->b, SB_BYTECODE);
    }
}

SBError sb_native_return(SBNativeCall *call, SBNativeValue value) {
    VM *v = call->vm;
    Value result;
    switch (value.type) {
    case SB_INTEGER:
        result = sb_int(value.as.integer);
        break;
    case SB_SINGLE:
        if (!isfinite(value.as.single))
            return SB_OVERFLOW;
        result = sb_float(value.as.single);
        break;
    case SB_STRING:
        if (value.as.string.length && !value.as.string.data)
            return SB_ARGUMENT;
        result = sb_string(v, value.as.string.data, value.as.string.length);
        break;
    default:
        return SB_TYPE;
    }
    sb_release(v, call->result);
    call->result = result;
    return v->b->error;
}

void sb_call_native(VM *v, uint8_t id, uint8_t argc) {
    if (argc > v->eval || v->top > v->floor || (unsigned)(v->floor - v->top) < argc * VALUE_SIZE) {
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    const SBNativeRegistry *registry = v->b->natives;
    SBNativeCall call = {v, argc, sb_int(0)};
    SBError error = callback(&registry->entries[id])(registry->user, &call);
    for (uint8_t remaining = argc; remaining; --remaining) {
        Value arg;
        if (!sb_pop(v, &arg))
            break;
        sb_release(v, arg);
    }
    if (error != SB_OK)
        sb_fail(v->b, error);
    if (v->b->error == SB_OK)
        sb_push(v, call.result);
    else
        sb_release(v, call.result);
}
