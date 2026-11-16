#include "internal.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef char
    float_must_be_ieee_binary32[(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24) ? 1
                                                                                             : -1];

int sb_integer_text(const char *text, int32_t *result) {
    uint32_t n = 0, limit;
    int negative = 0;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        ++text;
    }
    if (!*text)
        return 0;
    limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*text) {
        uint32_t digit;
        if (*text < '0' || *text > '9')
            return 0;
        digit = (uint32_t)(*text++ - '0');
        if (n > (limit - digit) / 10u)
            return 0;
        n = n * 10u + digit;
    }
    *result = negative ? (n == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)n) : (int32_t)n;
    return 1;
}

int sb_format(Value value, char *buffer, size_t capacity) {
    if (value.type == SB_INTEGER)
        return snprintf(buffer, capacity, "%" PRId32, value.as.i);
    if (value.type == SB_SINGLE)
        return snprintf(buffer, capacity, "%.9g", (double)value.as.f);
    return -1;
}
int sb_parse(const char *text, Value *value) {
    char *end;
    float result;
    double parsed;
    const char *p = text;
    int digits = 0;
    if (*p == '+' || *p == '-')
        ++p;
    while (*p >= '0' && *p <= '9') {
        ++digits;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++digits;
            ++p;
        }
    }
    if (!digits)
        return 0;
    if (*p == 'e' || *p == 'E') {
        int expdigits = 0;
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        while (*p >= '0' && *p <= '9') {
            ++expdigits;
            ++p;
        }
        if (!expdigits)
            return 0;
    }
    if (*p)
        return 0;
    errno = 0;
    parsed = strtod(text, &end);
    if (*end || errno == ERANGE || !isfinite(parsed) || parsed > FLT_MAX || parsed < -FLT_MAX)
        return 0;
    result = (float)parsed;
    if (parsed != 0 && result == 0)
        return 0;
    *value = sb_float(result);
    return 1;
}
static SBError integer_math(uint8_t op, int32_t a, int32_t b, Value *r) {
    int32_t n;
    switch (op) {
    case BC_ADD:
        if ((b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b))
            return SB_OVERFLOW;
        n = a + b;
        break;
    case BC_SUB:
        if ((b < 0 && a > INT32_MAX + b) || (b > 0 && a < INT32_MIN + b))
            return SB_OVERFLOW;
        n = a - b;
        break;
    case BC_MUL:
        if (a > 0 ? (b > 0 ? a > INT32_MAX / b : b < INT32_MIN / a)
                  : (a < 0 ? (b > 0 ? a < INT32_MIN / b : (b < 0 && a < INT32_MAX / b)) : 0))
            return SB_OVERFLOW;
        n = a * b;
        break;
    case BC_DIV:
    case BC_MOD:
        if (!b)
            return SB_ZERO_DIVISION;
        if (a == INT32_MIN && b == -1)
            return SB_OVERFLOW;
        n = op == BC_DIV ? a / b : a % b;
        break;
    case BC_EQ:
        n = a == b;
        break;
    case BC_NE:
        n = a != b;
        break;
    case BC_LT:
        n = a < b;
        break;
    case BC_LE:
        n = a <= b;
        break;
    case BC_GT:
        n = a > b;
        break;
    case BC_GE:
        n = a >= b;
        break;
    default:
        return SB_BYTECODE;
    }
    *r = sb_int(n);
    return SB_OK;
}
SBError sb_math(VM *v, uint8_t op, Value a, Value b, Value *r) {
    float x, y, n = 0;
    int compare = 0;
    if (a.type == SB_STRING && b.type == SB_STRING) {
        uint16_t an = sb_length(v, a), bn = sb_length(v, b);
        const uint8_t *ap = sb_bytes(v, a), *bp = sb_bytes(v, b);
        if (op == BC_ADD) {
            uint16_t p;
            if (an + bn > SB_STRING_MAX)
                return SB_MEMORY;
            *r = sb_zero(SB_STRING);
            if (!an && !bn)
                return SB_OK;
            p = sb_object(v, SB_STRING, (uint16_t)(an + bn + 2u));
            if (!p)
                return v->b->error;
            put16(v->b->pool + p + OBJ_HEADER, (uint16_t)(an + bn));
            memcpy(v->b->pool + p + OBJ_HEADER + 2u, ap, an);
            memcpy(v->b->pool + p + OBJ_HEADER + 2u + an, bp, bn);
            r->as.object = p;
            return SB_OK;
        }
        if (op < BC_EQ || op > BC_GE)
            return SB_TYPE;
        compare = memcmp(ap, bp, an < bn ? an : bn);
        if (!compare)
            compare = an < bn ? -1 : an > bn;
        return integer_math(op, compare, 0, r);
    }
    if (a.type > SB_SINGLE || b.type > SB_SINGLE)
        return SB_TYPE;
    if (a.type == SB_INTEGER && b.type == SB_INTEGER)
        return integer_math(op, a.as.i, b.as.i, r);
    if (op == BC_MOD)
        return SB_TYPE;
    x = a.type == SB_INTEGER ? (float)a.as.i : a.as.f;
    y = b.type == SB_INTEGER ? (float)b.as.i : b.as.f;
    if (!isfinite(x) || !isfinite(y))
        return SB_CONVERSION;
    switch (op) {
    case BC_ADD:
        n = x + y;
        break;
    case BC_SUB:
        n = x - y;
        break;
    case BC_MUL:
        n = x * y;
        break;
    case BC_DIV:
        if (y == 0)
            return SB_ZERO_DIVISION;
        n = x / y;
        break;
    case BC_EQ:
        compare = x == y;
        break;
    case BC_NE:
        compare = x != y;
        break;
    case BC_LT:
        compare = x < y;
        break;
    case BC_LE:
        compare = x <= y;
        break;
    case BC_GT:
        compare = x > y;
        break;
    case BC_GE:
        compare = x >= y;
        break;
    default:
        return SB_BYTECODE;
    }
    if (op >= BC_EQ && op <= BC_GE)
        *r = sb_int(compare);
    else {
        if (!isfinite(n))
            return SB_OVERFLOW;
        *r = sb_float(n);
    }
    return SB_OK;
}
