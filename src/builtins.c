#include "internal.h"
#include "rom.h"

static const char names[BI_COUNT][14] SB_ROM = {
    "ABS",     "MIN",         "MAX",          "SGN",        "INT",    "CINT",   "CSNG",
    "RND",     "SEED",        "LEN",          "LEFT$",      "RIGHT$", "MID$",   "CHR$",
    "ASC",     "STR$",        "VAL",          "UCASE$",     "LCASE$", "EEREAD", "EEWRITE",
    "PINMODE", "DIGITALREAD", "DIGITALWRITE", "ANALOGREAD", "DELAY",  "MILLIS"};
int sb_builtin_id(const char *name) {
    uint8_t i;
    for (i = 0; i < BI_COUNT; ++i)
        if (sb_rom_equal(name, names[i]))
            return i;
    return -1;
}
int sb_builtin_arity(uint8_t id, uint8_t count) {
    if (id >= BI_COUNT)
        return 0;
    if (id == BI_RND || id == BI_MILLIS)
        return count == 0;
    if (id == BI_MIN || id == BI_MAX || id == BI_LEFT || id == BI_RIGHT || id == BI_EEWRITE ||
        id == BI_PINMODE || id == BI_PINWRITE)
        return count == 2;
    if (id == BI_MID)
        return count == 2 || count == 3;
    return count == 1;
}
static int numeric(Value a) {
    return a.type == SB_INTEGER || a.type == SB_SINGLE;
}
static float as_float(Value a) {
    return a.type == SB_INTEGER ? (float)a.as.i : a.as.f;
}
static SBError integer_argument(Value a, int32_t *n) {
    if (a.type != SB_INTEGER)
        return SB_TYPE;
    *n = a.as.i;
    return SB_OK;
}
static SBError builtin_device(VM *v, uint8_t id, uint8_t count, Value *args, Value *r) {
    int32_t a = 0, b = 0, result = 0;
    uint8_t operation = (uint8_t)(id - BI_EEREAD);
    if (count && integer_argument(args[0], &a))
        return SB_TYPE;
    if (count > 1 && integer_argument(args[1], &b))
        return SB_TYPE;
    if (!v->platform || !v->platform->device)
        return SB_IO;
    if (id == BI_DELAY) {
        if (a < 0 || a > SB_DELAY_MAX)
            return SB_ARGUMENT;
        do {
            int32_t chunk = a > 10 ? 10 : a;
            if (!sb_poll(v))
                return v->b->error;
            if (v->platform->device(v->platform->user, SB_DELAY, chunk, 0, &result))
                return SB_IO;
            a -= chunk;
        } while (a);
    } else if (v->platform->device(v->platform->user, operation, a, b, &result))
        return SB_IO;
    *r = sb_int(result);
    return SB_OK;
}
SBError sb_builtin(VM *v, uint8_t id, uint8_t argc, Value *a, Value *r) {
    uint16_t length, start = 0, count, i;
    int32_t n;
    float f;
    char buffer[SB_STRING_MAX + 1u];
    if (!sb_builtin_arity(id, argc))
        return SB_ARITY;
    *r = sb_int(0);
    if (id >= BI_EEREAD)
        return builtin_device(v, id, argc, a, r);
    switch (id) {
    case BI_ABS:
        if (!numeric(a[0]))
            return SB_TYPE;
        if (a[0].type == SB_INTEGER) {
            if (a[0].as.i == INT32_MIN)
                return SB_OVERFLOW;
            *r = sb_int(a[0].as.i < 0 ? -a[0].as.i : a[0].as.i);
        } else
            *r = sb_float(fabsf(a[0].as.f));
        break;
    case BI_MIN:
    case BI_MAX:
        if (!numeric(a[0]) || !numeric(a[1]))
            return SB_TYPE;
        if (a[0].type == SB_INTEGER && a[1].type == SB_INTEGER) {
            *r = ((id == BI_MIN) == (a[0].as.i < a[1].as.i)) ? a[0] : a[1];
        } else {
            float x = as_float(a[0]), y = as_float(a[1]);
            *r = sb_float(((id == BI_MIN) == (x < y)) ? x : y);
        }
        break;
    case BI_SGN:
        if (!numeric(a[0]))
            return SB_TYPE;
        f = as_float(a[0]);
        *r = sb_int(f < 0 ? -1 : f > 0);
        break;
    case BI_INT:
    case BI_CINT:
        if (!numeric(a[0]))
            return SB_TYPE;
        if (a[0].type == SB_INTEGER) {
            *r = a[0];
            break;
        }
        f = id == BI_INT ? floorf(a[0].as.f) : truncf(a[0].as.f);
        if (!isfinite(f) || f >= 2147483648.0f || f < -2147483648.0f)
            return SB_CONVERSION;
        *r = sb_int((int32_t)f);
        break;
    case BI_CSNG:
        if (!numeric(a[0]))
            return SB_TYPE;
        *r = sb_float(as_float(a[0]));
        break;
    case BI_SEED:
        if (a[0].type != SB_INTEGER)
            return SB_TYPE;
        v->random = (uint32_t)a[0].as.i;
        if (!v->random)
            v->random = UINT32_C(0x6d2b79f5);
        break;
    case BI_RND:
        /* Mask before left shifts: equivalent xorshift32 modulo arithmetic,
         * without discarded high bits or sanitizer suppressions. */
        v->random ^= (v->random & UINT32_C(0x7ffff)) << 13;
        v->random ^= v->random >> 17;
        v->random ^= (v->random & UINT32_C(0x7ffffff)) << 5;
        *r = sb_float((float)(v->random >> 8) / 16777216.0f);
        break;
    case BI_LEN:
        if (a[0].type != SB_STRING)
            return SB_TYPE;
        *r = sb_int(sb_length(v, a[0]));
        break;
    case BI_LEFT:
    case BI_RIGHT:
    case BI_MID:
        if (a[0].type != SB_STRING || a[1].type != SB_INTEGER)
            return SB_TYPE;
        length = sb_length(v, a[0]);
        n = a[1].as.i;
        if (id == BI_MID) {
            if (n < 1 || n > (int32_t)length + 1)
                return SB_ARGUMENT;
            start = (uint16_t)(n - 1);
            count = (uint16_t)(length - start);
            if (argc == 3) {
                if (a[2].type != SB_INTEGER)
                    return SB_TYPE;
                if (a[2].as.i < 0)
                    return SB_ARGUMENT;
                if (a[2].as.i < count)
                    count = (uint16_t)a[2].as.i;
            }
        } else {
            if (n < 0)
                return SB_ARGUMENT;
            count = n > length ? length : (uint16_t)n;
            if (id == BI_RIGHT)
                start = (uint16_t)(length - count);
        }
        *r = sb_string(v, sb_bytes(v, a[0]) + start, count);
        break;
    case BI_CHR:
        if (a[0].type != SB_INTEGER)
            return SB_TYPE;
        if (a[0].as.i < 0 || a[0].as.i > 255)
            return SB_ARGUMENT;
        buffer[0] = (char)(uint8_t)a[0].as.i;
        *r = sb_string(v, (uint8_t *)buffer, 1);
        break;
    case BI_ASC:
        if (a[0].type != SB_STRING)
            return SB_TYPE;
        if (!sb_length(v, a[0]))
            return SB_ARGUMENT;
        *r = sb_int(sb_bytes(v, a[0])[0]);
        break;
    case BI_STR:
        if (!numeric(a[0]))
            return SB_TYPE;
        n = sb_format(a[0], buffer, sizeof(buffer));
        if (n < 0 || (uint32_t)n >= sizeof(buffer))
            return SB_CONVERSION;
        *r = sb_string(v, (uint8_t *)buffer, (uint16_t)n);
        break;
    case BI_VAL:
        if (a[0].type != SB_STRING)
            return SB_TYPE;
        length = sb_length(v, a[0]);
        memcpy(buffer, sb_bytes(v, a[0]), length);
        buffer[length] = 0;
        if (memchr(buffer, 0, length) || !sb_parse(buffer, r))
            return SB_CONVERSION;
        break;
    case BI_UCASE:
    case BI_LCASE:
        if (a[0].type != SB_STRING)
            return SB_TYPE;
        length = sb_length(v, a[0]);
        for (i = 0; i < length; ++i) {
            uint8_t ch = sb_bytes(v, a[0])[i];
            if (id == BI_UCASE && ch >= 'a' && ch <= 'z')
                ch = (uint8_t)(ch - 32u);
            if (id == BI_LCASE && ch >= 'A' && ch <= 'Z')
                ch = (uint8_t)(ch + 32u);
            buffer[i] = (char)ch;
        }
        *r = sb_string(v, (uint8_t *)buffer, length);
        break;
    default:
        return SB_BYTECODE;
    }
    return v->b->error;
}
