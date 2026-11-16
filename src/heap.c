#include "internal.h"

Value sb_int(int32_t n) {
    Value v;
    v.type = SB_INTEGER;
    v.as.i = n;
    return v;
}
Value sb_float(float n) {
    Value v;
    v.type = SB_SINGLE;
    v.as.f = n;
    return v;
}
Value sb_zero(uint8_t type) {
    Value v = sb_int(0);
    v.type = type;
    return v;
}
Value sb_value_get(const uint8_t *p, uint8_t type) {
    Value v;
    v.type = type;
    if (type == SB_SINGLE)
        memcpy(&v.as.f, p, 4);
    else if (type == SB_STRING || (type & FLAG_ARRAY))
        v.as.object = get16(p);
    else
        v.as.i = get32(p);
    return v;
}
void sb_value_put(uint8_t *p, Value v) {
    if (v.type == SB_SINGLE)
        memcpy(p, &v.as.f, 4);
    else if (v.type == SB_STRING || (v.type & FLAG_ARRAY)) {
        put16(p, v.as.object);
        put16(p + 2, 0);
    } else
        put32(p, v.as.i);
}
static int object_type(Value v) {
    return v.type == SB_STRING || (v.type & FLAG_ARRAY);
}
static int valid_object(VM *v, uint16_t p) {
    uint16_t size;
    if (p < v->bottom || p >= v->heap || v->heap - p < OBJ_HEADER)
        return 0;
    size = get16(v->b->pool + p);
    return size >= OBJ_HEADER && size <= v->heap - p && get16(v->b->pool + p + 2) != 0;
}
void sb_retain(VM *v, Value value) {
    uint16_t refs;
    if (!object_type(value) || !value.as.object)
        return;
    if (!valid_object(v, value.as.object)) {
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    refs = get16(v->b->pool + value.as.object + 2);
    if (refs == UINT16_MAX) {
        sb_fail(v->b, SB_MEMORY);
        return;
    }
    put16(v->b->pool + value.as.object + 2, (uint16_t)(refs + 1u));
}
static void merge(VM *v) {
    uint16_t p = v->bottom, previous = NONE;
    while (p < v->heap) {
        uint16_t n = get16(v->b->pool + p);
        if (!get16(v->b->pool + p + 2)) {
            if (previous != NONE) {
                put16(v->b->pool + previous, (uint16_t)(get16(v->b->pool + previous) + n));
            } else
                previous = p;
        } else
            previous = NONE;
        p = (uint16_t)(p + n);
    }
    if (previous != NONE)
        v->heap = previous;
}
void sb_release(VM *v, Value value) {
    uint16_t p, refs, count, i;
    if (!object_type(value) || !value.as.object)
        return;
    p = value.as.object;
    if (!valid_object(v, p)) {
        sb_fail(v->b, SB_BYTECODE);
        return;
    }
    refs = get16(v->b->pool + p + 2);
    put16(v->b->pool + p + 2, (uint16_t)(refs - 1u));
    if (refs != 1)
        return;
    /* Arrays contain scalar cells only. Releasing string elements is a flat
     * loop, never a recursive descent through an object graph. */
    if ((value.type & FLAG_ARRAY) && (value.type & TYPE_MASK) == SB_STRING) {
        count = get16(v->b->pool + p + OBJ_HEADER);
        for (i = 0; i < count; ++i) {
            uint16_t child = get16(v->b->pool + p + OBJ_HEADER + 4u + i * 4u);
            if (child) {
                if (!valid_object(v, child)) {
                    sb_fail(v->b, SB_BYTECODE);
                    return;
                }
                refs = get16(v->b->pool + child + 2);
                put16(v->b->pool + child + 2, (uint16_t)(refs - 1u));
            }
        }
    }
    merge(v);
}
uint16_t sb_object(VM *v, uint8_t kind, uint16_t payload) {
    uint16_t p, total;
    if (payload > SB_POOL_SIZE - OBJ_HEADER) {
        sb_fail(v->b, SB_MEMORY);
        return 0;
    }
    total = (uint16_t)(payload + OBJ_HEADER);
    for (p = v->bottom; p < v->heap;) {
        uint16_t n = get16(v->b->pool + p);
        if (!get16(v->b->pool + p + 2) && n >= total) {
            if (n - total >= OBJ_HEADER) {
                put16(v->b->pool + p + total, (uint16_t)(n - total));
                put16(v->b->pool + p + total + 2, 0);
            } else
                total = n;
            break;
        }
        p = (uint16_t)(p + n);
    }
    if (p == v->heap) {
        if (!sb_room(v, total))
            return 0;
        v->heap = (uint16_t)(v->heap + total);
    }
    memset(v->b->pool + p, 0, total);
    put16(v->b->pool + p, total);
    put16(v->b->pool + p + 2, 1);
    v->b->pool[p + 4] = kind;
    sb_peaks(v);
    return p;
}
Value sb_string(VM *v, const uint8_t *bytes, uint16_t length) {
    Value result = sb_zero(SB_STRING);
    if (length > SB_STRING_MAX) {
        sb_fail(v->b, SB_MEMORY);
        return result;
    }
    if (!length)
        return result;
    result.as.object = sb_object(v, SB_STRING, (uint16_t)(length + 2u));
    if (result.as.object) {
        put16(v->b->pool + result.as.object + OBJ_HEADER, length);
        memcpy(v->b->pool + result.as.object + OBJ_HEADER + 2u, bytes, length);
    }
    return result;
}
uint16_t sb_length(VM *v, Value value) {
    if (value.type != SB_STRING) {
        sb_fail(v->b, SB_TYPE);
        return 0;
    }
    if (!value.as.object)
        return 0;
    if (!valid_object(v, value.as.object)) {
        sb_fail(v->b, SB_BYTECODE);
        return 0;
    }
    return get16(v->b->pool + value.as.object + OBJ_HEADER);
}
const uint8_t *sb_bytes(VM *v, Value value) {
    return value.as.object ? v->b->pool + value.as.object + OBJ_HEADER + 2u : v->b->pool;
}
int sb_convert(VM *v, Value *value, uint8_t type) {
    if (value->type == type)
        return 1;
    if (value->type == SB_INTEGER && type == SB_SINGLE) {
        *value = sb_float((float)value->as.i);
        return 1;
    }
    sb_fail(v->b, SB_TYPE);
    return 0;
}
int sb_truth(VM *v, Value value) {
    if (value.type == SB_INTEGER)
        return value.as.i != 0;
    if (value.type == SB_SINGLE && isfinite(value.as.f))
        return value.as.f != 0;
    sb_fail(v->b, SB_TYPE);
    return 0;
}
uint16_t sb_slot_symbol(SBasic *b, uint16_t function, uint8_t slot) {
    uint16_t p = 0;
    while (p < b->symbols_end) {
        uint8_t *s = b->pool + p;
        if (s[0] == (function == NONE ? SY_GLOBAL : SY_LOCAL) && get16(s + 2) == function &&
            s[4] == slot)
            return p;
        p = (uint16_t)(p + SYMBOL_HEADER + s[1] + 1u);
    }
    return NONE;
}
