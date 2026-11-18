#include "internal.h"
#include "rom.h"

enum {
    T_EOF = 256,
    T_NL,
    T_ID,
    T_NUM,
    T_LE,
    T_GE,
    T_NE,
    T_REAL,
    T_STRING,
    K_LET,
    K_PRINT,
    K_END,
    K_IF,
    K_THEN,
    K_ELSEIF,
    K_ELSE,
    K_WHILE,
    K_WEND,
    K_FUNCTION,
    K_RETURN,
    K_LOCAL,
    K_MOD,
    K_REM,
    K_INPUT,
    K_STOP,
    K_CONST,
    K_DIM,
    K_AS,
    K_INTEGER,
    K_SINGLE,
    K_STRING,
    K_AND,
    K_OR,
    K_NOT,
    K_SELECT,
    K_CASE,
    K_FOR,
    K_TO,
    K_STEP,
    K_NEXT,
    K_DO,
    K_LOOP,
    K_UNTIL,
    K_REPEAT,
    K_EXIT,
    K_SUB,
    K_CALL
};
typedef struct {
    SBSource source;
    uint32_t pos, number, start;
    float real;
    uint16_t length;
    uint8_t text[SB_TOKEN_MAX + 1u];
    uint16_t line, token_line;
    int token;
    char name[SB_NAME_MAX + 1];
} Lexer;
typedef struct {
    uint16_t symbol;
    uint8_t kind, op, argc;
} Operator;
enum { EXPR_NATIVE_CALL = 5 };
typedef struct {
    uint16_t start, pending, chain, exits, symbol;
    uint8_t kind, otherwise, flags;
} Block;
typedef struct {
    SBasic *b;
    Lexer lex;
    Operator ops[SB_EXPR_MAX];
    Block blocks[SB_BLOCK_MAX];
    uint16_t function, skip;
    uint8_t op_count, block_count, allow_sub;
} Compiler;

static int alpha(int ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}
static int digit(int ch) {
    return ch >= '0' && ch <= '9';
}
static int peek(Lexer *l) {
    return l->pos < l->source.length ? l->source.read(l->source.user, l->pos) : -1;
}
static int keyword(const char *s) {
    static const char words[][10] SB_ROM = {
        "LET",   "PRINT",    "END",    "IF",      "THEN",   "ELSEIF", "ELSE",  "WHILE",
        "WEND",  "FUNCTION", "RETURN", "LOCAL",   "MOD",    "REM",    "INPUT", "STOP",
        "CONST", "DIM",      "AS",     "INTEGER", "SINGLE", "STRING", "AND",   "OR",
        "NOT",   "SELECT",   "CASE",   "FOR",     "TO",     "STEP",   "NEXT",  "DO",
        "LOOP",  "UNTIL",    "REPEAT", "EXIT",    "SUB",    "CALL"};
    unsigned i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (sb_rom_equal(s, words[i]))
            return K_LET + (int)i;
    return T_ID;
}
static void lex_next(Compiler *c) {
    Lexer *l = &c->lex;
    int ch, following;
    uint8_t n;
    if (c->b->error != SB_OK) {
        l->token = T_EOF;
        return;
    }
    do {
        ch = peek(l);
        if (ch == ' ' || ch == '\t' || ch == '\r')
            ++l->pos;
    } while (ch == ' ' || ch == '\t' || ch == '\r');
    c->b->line = l->token_line = l->line;
    l->start = l->pos;
    if (ch == -1) {
        l->token = T_EOF;
        return;
    }
    if (ch < 0 || ch > 127 || ch == 0) {
        sb_fail(c->b, SB_SYNTAX);
        l->token = T_EOF;
        return;
    }
    ++l->pos;
    if (ch == '\n') {
        l->token = T_NL;
        if (l->line == SB_LINE_LIMIT)
            sb_fail(c->b, SB_COMPLEXITY);
        else
            ++l->line;
        return;
    }
    if (alpha(ch)) {
        n = 0;
        do {
            if (n == SB_NAME_MAX) {
                sb_fail(c->b, SB_NAME);
                l->token = T_EOF;
                return;
            }
            l->name[n++] = (char)(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
            ch = peek(l);
            if (!alpha(ch) && !digit(ch))
                break;
            ++l->pos;
        } while (1);
        if (ch == '$') {
            if (n == SB_NAME_MAX) {
                sb_fail(c->b, SB_NAME);
                l->token = T_EOF;
                return;
            }
            l->name[n++] = '$';
            ++l->pos;
        }
        l->name[n] = '\0';
        l->token = keyword(l->name);
        if (l->token != K_REM)
            return;
        ch = '\'';
    }
    if (ch == '\'') {
        while ((ch = peek(l)) != -1 && ch != '\n')
            ++l->pos;
        if (ch == '\n') {
            ++l->pos;
            l->token = T_NL;
            if (l->line == SB_LINE_LIMIT)
                sb_fail(c->b, SB_COMPLEXITY);
            else
                ++l->line;
        } else
            l->token = T_EOF;
        return;
    }
    if (ch == '"') {
        l->length = 0;
        for (;;) {
            ch = peek(l);
            if (ch < 1 || ch == '\n' || ch == '\r') {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            ++l->pos;
            if (ch == '"') {
                if (peek(l) != '"')
                    break;
                ++l->pos;
            }
            if (l->length == SB_STRING_MAX) {
                sb_fail(c->b, SB_MEMORY);
                break;
            }
            l->text[l->length++] = (uint8_t)ch;
        }
        l->text[l->length] = 0;
        l->token = T_STRING;
        return;
    }
    if (digit(ch) || (ch == '.' && digit(peek(l)))) {
        int real = ch == '.', exponent = 0;
        l->length = 0;
        l->text[l->length++] = (uint8_t)ch;
        while (1) {
            ch = peek(l);
            if (digit(ch)) {
            } else if (ch == '.' && !real && !exponent)
                real = 1;
            else if ((ch == 'e' || ch == 'E') && !exponent) {
                real = exponent = 1;
            } else if ((ch == '+' || ch == '-') && l->length &&
                       (l->text[l->length - 1u] == 'e' || l->text[l->length - 1u] == 'E')) {
            } else
                break;
            if (l->length == SB_NUMBER_MAX) {
                sb_fail(c->b, SB_NUMBER);
                l->token = T_EOF;
                return;
            }
            l->text[l->length++] = (uint8_t)ch;
            ++l->pos;
        }
        l->text[l->length] = 0;
        if (real) {
            Value value;
            if (!sb_parse((char *)l->text, &value)) {
                sb_fail(c->b, SB_NUMBER);
                l->token = T_EOF;
                return;
            }
            l->real = value.as.f;
            l->token = T_REAL;
        } else {
            uint16_t i;
            l->number = 0;
            for (i = 0; i < l->length; ++i) {
                uint32_t d = (uint32_t)(l->text[i] - '0');
                if (l->number > (UINT32_C(2147483648) - d) / 10u) {
                    sb_fail(c->b, SB_NUMBER);
                    l->token = T_EOF;
                    return;
                }
                l->number = l->number * 10u + d;
            }
            l->token = T_NUM;
        }
        return;
    }
    following = peek(l);
    l->token = ch;
    if (ch == '<' && following == '=')
        l->token = T_LE;
    if (ch == '<' && following == '>')
        l->token = T_NE;
    if (ch == '>' && following == '=')
        l->token = T_GE;
    if (l->token != ch)
        ++l->pos;
}
static int is_name(int token) {
    return token == T_ID || (token >= K_INPUT && token <= K_CALL);
}
static int statement_token(Compiler *c) {
    uint32_t p = c->lex.pos;
    int ch;
    if (c->lex.token < K_INPUT || c->lex.token > K_CALL)
        return c->lex.token;
    do {
        ch = p < c->lex.source.length ? c->lex.source.read(c->lex.source.user, p) : -1;
        ++p;
    } while (ch == ' ' || ch == '\t');
    return ch == '=' || ch == '(' ? T_ID : c->lex.token;
}
static void rewind_source(Compiler *c) {
    c->lex.pos = 0;
    c->lex.line = 1;
    c->function = NONE;
    c->block_count = c->op_count = 0;
    lex_next(c);
}
static int accept(Compiler *c, int token) {
    if (c->lex.token != token)
        return 0;
    lex_next(c);
    return 1;
}
static int expect(Compiler *c, int token) {
    if (token == T_ID && is_name(c->lex.token)) {
        lex_next(c);
        return 1;
    }
    if (accept(c, token))
        return 1;
    sb_fail(c->b, SB_SYNTAX);
    return 0;
}
static int line_end(Compiler *c) {
    if (c->lex.token != T_EOF && c->lex.token != T_NL) {
        sb_fail(c->b, SB_SYNTAX);
        return 0;
    }
    return 1;
}

static uint16_t find(Compiler *c, uint8_t kind, uint16_t owner, const char *name) {
    uint16_t p = 0;
    while (p < c->b->symbols_end) {
        uint8_t *s = c->b->pool + p;
        if (s[0] == kind && get16(s + 2) == owner && !strcmp((char *)s + SYMBOL_HEADER, name))
            return p;
        p = (uint16_t)(p + SYMBOL_HEADER + s[1] + 1u);
    }
    return NONE;
}
static uint16_t add(Compiler *c, uint8_t kind, uint16_t owner, const char *name) {
    SBasic *b = c->b;
    uint16_t p = b->symbols_end;
    size_t n = strlen(name), size = SYMBOL_HEADER + n + 1u;
    uint8_t slot = 0;
    if (find(c, kind, owner, name) != NONE ||
        (kind == SY_GLOBAL && find(c, SY_FUNCTION, NONE, name) != NONE) ||
        (kind == SY_FUNCTION && find(c, SY_GLOBAL, NONE, name) != NONE)) {
        sb_fail(b, SB_DUPLICATE);
        return NONE;
    }
    if (kind == SY_GLOBAL) {
        if (b->globals == SB_SLOT_MAX) {
            sb_fail(b, SB_COMPLEXITY);
            return NONE;
        }
        slot = (uint8_t)b->globals;
    } else if (kind == SY_LOCAL) {
        if (b->pool[owner + 6] == SB_SLOT_MAX) {
            sb_fail(b, SB_COMPLEXITY);
            return NONE;
        }
        slot = b->pool[owner + 6];
    }
    if (size > SB_POOL_SIZE - p) {
        sb_fail(b, SB_MEMORY);
        return NONE;
    }
    memset(b->pool + p, 0, size);
    b->pool[p] = kind;
    b->pool[p + 1] = (uint8_t)n;
    put16(b->pool + p + 2, owner);
    b->pool[p + 4] = slot;
    b->pool[p + 7] = (uint8_t)(n && name[n - 1u] == '$' ? SB_STRING : SB_INTEGER);
    memcpy(b->pool + p + SYMBOL_HEADER, name, n + 1u);
    b->symbols_end = (uint16_t)(p + size);
    if (kind == SY_GLOBAL)
        ++b->globals;
    if (kind == SY_LOCAL)
        ++b->pool[owner + 6];
    return p;
}

static uint8_t parse_type(Compiler *c, uint8_t type) {
    if (accept(c, K_AS)) {
        if (accept(c, K_INTEGER))
            type = SB_INTEGER;
        else if (accept(c, K_SINGLE))
            type = SB_SINGLE;
        else if (accept(c, K_STRING))
            type = SB_STRING;
        else
            sb_fail(c->b, SB_SYNTAX);
    }
    return type;
}
static uint16_t declare(Compiler *c, uint8_t kind, int parameter) {
    char name[SB_NAME_MAX + 1];
    uint8_t type, array = 0;
    uint16_t symbol;
    unsigned nesting;
    if (!is_name(c->lex.token)) {
        sb_fail(c->b, SB_SYNTAX);
        return NONE;
    }
    memcpy(name, c->lex.name, sizeof(name));
    type = (uint8_t)(name[strlen(name) - 1u] == '$' ? SB_STRING : SB_INTEGER);
    lex_next(c);
    if (accept(c, '(')) {
        if (parameter) {
            sb_fail(c->b, SB_TYPE);
            return NONE;
        }
        array = FLAG_ARRAY;
        nesting = 1;
        while (nesting && c->lex.token != T_EOF && c->lex.token != T_NL) {
            if (c->lex.token == '(')
                ++nesting;
            if (c->lex.token == ')')
                --nesting;
            lex_next(c);
        }
        if (nesting) {
            sb_fail(c->b, SB_SYNTAX);
            return NONE;
        }
    }
    type = parse_type(c, type);
    if (name[strlen(name) - 1u] == '$' && type != SB_STRING) {
        sb_fail(c->b, SB_TYPE);
        return NONE;
    }
    symbol = find(c, kind, kind == SY_GLOBAL ? NONE : c->function, name);
    if (symbol != NONE && kind == SY_GLOBAL && !(c->b->pool[symbol + 7] & FLAG_DECLARED)) {
    } else
        symbol = add(c, kind, kind == SY_GLOBAL ? NONE : c->function, name);
    if (symbol != NONE)
        c->b->pool[symbol + 7] = (uint8_t)(type | array | FLAG_DECLARED);
    return symbol;
}
/* Discovery retains the stage-one symbol layout and bytecode for old programs. */
static void discover(Compiler *c) {
    uint16_t f, s;
    int token;
    char name[SB_NAME_MAX + 1];
    while (c->lex.token != T_EOF && c->b->error == SB_OK) {
        if (accept(c, T_NL))
            continue;
        token = statement_token(c);
        if (token == K_FUNCTION || token == K_SUB) {
            lex_next(c);
            if (c->function != NONE || !is_name(c->lex.token)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            f = add(c, SY_FUNCTION, NONE, c->lex.name);
            if (f == NONE)
                break;
            c->function = f;
            lex_next(c);
            if (!expect(c, '('))
                break;
            if (c->lex.token != ')')
                do {
                    if (declare(c, SY_LOCAL, 1) == NONE)
                        break;
                    ++c->b->pool[f + 5];
                } while (accept(c, ','));
            if (!expect(c, ')'))
                break;
            c->b->pool[f + 7] = parse_type(c, c->b->pool[f + 7]);
            if (token == K_SUB) {
                if ((c->b->pool[f + 7] & TYPE_MASK) != SB_INTEGER) {
                    sb_fail(c->b, SB_TYPE);
                    break;
                }
                c->b->pool[f + 7] |= FLAG_SUB;
            }
            if (!line_end(c))
                break;
        } else if (token == K_LOCAL || token == K_DIM || token == K_CONST) {
            lex_next(c);
            if ((token == K_LOCAL && c->function == NONE) ||
                (token == K_CONST && c->function != NONE)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            do {
                s = declare(c, c->function == NONE ? SY_GLOBAL : SY_LOCAL, 0);
                if (s == NONE)
                    break;
                if (token == K_CONST) {
                    if (c->b->pool[s + 7] & FLAG_ARRAY) {
                        sb_fail(c->b, SB_TYPE);
                        break;
                    }
                    c->b->pool[s + 7] |= FLAG_CONST;
                    if (!expect(c, '='))
                        break;
                    /* The second pass validates/evaluates its initializer. */
                    break;
                }
            } while (accept(c, ','));
            if (token != K_CONST && !line_end(c))
                break;
        } else if (accept(c, K_END)) {
            if (accept(c, K_FUNCTION) || accept(c, K_SUB)) {
                if (c->function == NONE) {
                    sb_fail(c->b, SB_SYNTAX);
                    break;
                }
                c->function = NONE;
            }
        } else {
            if (token == K_FOR || token == K_LET || token == K_INPUT)
                lex_next(c);
            if (token == K_INPUT && c->lex.token == T_STRING) {
                lex_next(c);
                expect(c, ';');
            }
            if (c->function == NONE && is_name(c->lex.token)) {
                do {
                    memcpy(name, c->lex.name, sizeof(name));
                    lex_next(c);
                    if ((c->lex.token == '=' || token == K_INPUT) &&
                        find(c, SY_GLOBAL, NONE, name) == NONE)
                        add(c, SY_GLOBAL, NONE, name);
                    if (token != K_INPUT || !accept(c, ','))
                        break;
                    if (!is_name(c->lex.token)) {
                        sb_fail(c->b, SB_SYNTAX);
                        break;
                    }
                } while (1);
            }
        }
        while (c->lex.token != T_EOF && c->lex.token != T_NL)
            lex_next(c);
    }
    if (c->function != NONE)
        sb_fail(c->b, SB_SYNTAX);
}

static int emit(Compiler *c, uint8_t value) {
    SBasic *b = c->b;
    if (b->error != SB_OK)
        return 0;
    if (b->program_end == SB_POOL_SIZE) {
        sb_fail(b, SB_MEMORY);
        return 0;
    }
    b->pool[b->program_end++] = value;
    return 1;
}
static void word(Compiler *c, uint16_t value) {
    emit(c, (uint8_t)value);
    emit(c, (uint8_t)(value >> 8));
}
static void constant(Compiler *c, int32_t value) {
    uint32_t u = (uint32_t)value;
    emit(c, BC_CONST);
    emit(c, (uint8_t)u);
    emit(c, (uint8_t)(u >> 8));
    emit(c, (uint8_t)(u >> 16));
    emit(c, (uint8_t)(u >> 24));
}
static void text_constant(Compiler *c, const uint8_t *text, uint16_t length) {
    uint16_t i;
    emit(c, BC_SCONST);
    word(c, length);
    for (i = 0; i < length; ++i)
        emit(c, text[i]);
}
static uint16_t branch(Compiler *c, uint8_t op, uint16_t target) {
    uint16_t p;
    emit(c, op);
    p = c->b->program_end;
    word(c, target);
    return p;
}
static void patch(Compiler *c, uint16_t p, uint16_t target) {
    if (c->b->error == SB_OK)
        put16(c->b->pool + p, target);
}
static uint16_t lookup_variable(Compiler *c, const char *name) {
    uint16_t s = NONE;
    if (c->function != NONE)
        s = find(c, SY_LOCAL, c->function, name);
    if (s == NONE)
        s = find(c, SY_GLOBAL, NONE, name);
    return s;
}
static uint16_t variable(Compiler *c, const char *name) {
    uint16_t s = lookup_variable(c, name);
    if (s == NONE)
        sb_fail(c->b, SB_UNKNOWN);
    return s;
}
static void access_variable(Compiler *c, uint16_t s, int store) {
    if (s == NONE)
        return;
    if (c->b->pool[s + 7] & FLAG_ARRAY) {
        sb_fail(c->b, SB_TYPE);
        return;
    }
    emit(c, (uint8_t)(c->b->pool[s] == SY_GLOBAL ? (store ? BC_STORE_G : BC_LOAD_G)
                                                 : (store ? BC_STORE_L : BC_LOAD_L)));
    emit(c, c->b->pool[s + 4]);
}
static uint8_t precedence(uint8_t op) {
    if (op == BC_NEG)
        return 7;
    if (op == BC_MUL || op == BC_DIV || op == BC_MOD)
        return 6;
    if (op == BC_ADD || op == BC_SUB)
        return 5;
    if (op == BC_NOT)
        return 3;
    if (op == BC_AND)
        return 2;
    if (op == BC_OR)
        return 1;
    return 4;
}
static uint8_t binary(int token) {
    switch (token) {
    case '+':
        return BC_ADD;
    case '-':
        return BC_SUB;
    case '*':
        return BC_MUL;
    case '/':
        return BC_DIV;
    case K_MOD:
        return BC_MOD;
    case '=':
        return BC_EQ;
    case T_NE:
        return BC_NE;
    case '<':
        return BC_LT;
    case T_LE:
        return BC_LE;
    case '>':
        return BC_GT;
    case T_GE:
        return BC_GE;
    case K_AND:
        return BC_AND;
    case K_OR:
        return BC_OR;
    default:
        return 0;
    }
}
static int operator_push(Compiler *c, uint8_t kind, uint8_t op, uint16_t sym) {
    Operator *o;
    if (c->op_count == SB_EXPR_MAX) {
        sb_fail(c->b, SB_COMPLEXITY);
        return 0;
    }
    o = &c->ops[c->op_count++];
    o->kind = kind;
    o->op = op;
    o->symbol = sym;
    o->argc = 0;
    return 1;
}
static void emit_operator(Compiler *c) {
    Operator o = c->ops[--c->op_count];
    if (o.op == BC_AND || o.op == BC_OR) {
        emit(c, BC_BOOL);
        patch(c, o.symbol, c->b->program_end);
    } else
        emit(c, o.op);
}
static void flush_operators(Compiler *c) {
    while (c->op_count && c->ops[c->op_count - 1].kind == 0)
        emit_operator(c);
}

/* Iterative shunting-yard, including call argument markers. No recursive
 * expression descent and no C recursion for nested calls or parentheses. */
static void expression(Compiler *c) {
    int want = 1, marker;
    uint8_t op, count;
    uint16_t s;
    Operator o;
    c->op_count = 0;
    while (c->b->error == SB_OK) {
        if (want) {
            if (c->lex.token == K_NOT && find(c, SY_FUNCTION, NONE, c->lex.name) == NONE &&
                lookup_variable(c, c->lex.name) == NONE) {
                lex_next(c);
                operator_push(c, 0, BC_NOT, NONE);
                continue;
            }
            if (accept(c, '+'))
                continue;
            if (accept(c, '-')) {
                operator_push(c, 0, BC_NEG, NONE);
                continue;
            }
            if (c->lex.token == T_NUM) {
                if (c->lex.number == UINT32_C(2147483648)) {
                    if (!c->op_count || c->ops[c->op_count - 1].kind != 0 ||
                        c->ops[c->op_count - 1].op != BC_NEG) {
                        sb_fail(c->b, SB_NUMBER);
                        break;
                    }
                    --c->op_count;
                    constant(c, INT32_MIN);
                } else
                    constant(c, (int32_t)c->lex.number);
                lex_next(c);
                want = 0;
                continue;
            }
            if (c->lex.token == T_REAL) {
                uint8_t bytes[4];
                unsigned i;
                memcpy(bytes, &c->lex.real, 4);
                emit(c, BC_FCONST);
                for (i = 0; i < 4; ++i)
                    emit(c, bytes[i]);
                lex_next(c);
                want = 0;
                continue;
            }
            if (c->lex.token == T_STRING) {
                text_constant(c, c->lex.text, c->lex.length);
                lex_next(c);
                want = 0;
                continue;
            }
            if (is_name(c->lex.token)) {
                char name[SB_NAME_MAX + 1];
                memcpy(name, c->lex.name, sizeof(name));
                lex_next(c);
                if (accept(c, '(')) {
                    int builtin = sb_builtin_id(name);
                    s = find(c, SY_FUNCTION, NONE, name);
                    if (s != NONE) {
                        if ((c->b->pool[s + 7] & FLAG_SUB) && (!c->allow_sub || c->op_count)) {
                            sb_fail(c->b, SB_TYPE);
                            break;
                        }
                        operator_push(c, 2, 0, s);
                        continue;
                    }
                    if (builtin >= 0) {
                        operator_push(c, 4, 0, (uint16_t)builtin);
                        continue;
                    }
                    int native = sb_native_id(c->b->natives, name);
                    if (native >= 0 && lookup_variable(c, name) == NONE) {
                        operator_push(c, EXPR_NATIVE_CALL, 0, (uint16_t)native);
                        continue;
                    }
                    s = variable(c, name);
                    if (s == NONE)
                        break;
                    if (!(c->b->pool[s + 7] & FLAG_ARRAY)) {
                        sb_fail(c->b, SB_TYPE);
                        break;
                    }
                    operator_push(c, 3, 0, s);
                    continue;
                }
                access_variable(c, variable(c, name), 0);
                want = 0;
                continue;
            }
            if (accept(c, '(')) {
                operator_push(c, 1, 0, NONE);
                continue;
            }
            /* An empty call is the only valid close while expecting an operand. */
            if (c->lex.token != ')' || !c->op_count || c->ops[c->op_count - 1].kind < 2 ||
                c->ops[c->op_count - 1].argc) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
        }
        op = binary(c->lex.token);
        if (op && !want) {
            while (c->op_count && c->ops[c->op_count - 1].kind == 0 &&
                   precedence(c->ops[c->op_count - 1].op) >= precedence(op))
                emit_operator(c);
            s = NONE;
            if (op == BC_AND || op == BC_OR) {
                emit(c, BC_BOOL);
                emit(c, BC_DUP);
                s = branch(c, op == BC_AND ? BC_JZ : BC_JNZ, NONE);
                emit(c, BC_DROP);
            }
            operator_push(c, 0, op, s);
            lex_next(c);
            want = 1;
            continue;
        }
        if (c->lex.token == ',' || c->lex.token == ')') {
            marker = (int)c->op_count - 1;
            while (marker >= 0 && c->ops[marker].kind == 0)
                --marker;
            if (marker < 0)
                break;
            flush_operators(c);
            if (c->lex.token == ',') {
                if (want || c->ops[marker].kind < 2) {
                    sb_fail(c->b, SB_SYNTAX);
                    break;
                }
                if (c->ops[marker].argc == 254) {
                    sb_fail(c->b, SB_ARITY);
                    break;
                }
                ++c->ops[marker].argc;
                lex_next(c);
                want = 1;
                continue;
            }
            o = c->ops[--c->op_count];
            if (o.kind >= 2) {
                count = want ? 0 : (uint8_t)(o.argc + 1u);
                if (o.kind == 2) {
                    if (count != c->b->pool[o.symbol + 5]) {
                        sb_fail(c->b, SB_ARITY);
                        break;
                    }
                    emit(c, BC_CALL);
                    word(c, o.symbol);
                    emit(c, count);
                } else if (o.kind == 3) {
                    if (count != 1) {
                        sb_fail(c->b, SB_ARITY);
                        break;
                    }
                    emit(c, BC_ARRAY_GET);
                    word(c, o.symbol);
                } else {
                    int native = o.kind == EXPR_NATIVE_CALL;
                    uint8_t id = (uint8_t)o.symbol;
                    if (!(native ? sb_native_arity(c->b->natives, id, count)
                                 : sb_builtin_arity(id, count))) {
                        sb_fail(c->b, SB_ARITY);
                        break;
                    }
                    emit(c, native ? BC_NATIVE : BC_BUILTIN);
                    emit(c, id);
                    emit(c, count);
                }
            } else if (want) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            lex_next(c);
            want = 0;
            if (c->allow_sub && !c->op_count)
                break;
            continue;
        }
        break;
    }
    if (want)
        sb_fail(c->b, SB_SYNTAX);
    flush_operators(c);
    if (c->op_count)
        sb_fail(c->b, SB_SYNTAX);
}

static Block *block_push(Compiler *c, uint8_t kind, uint16_t start, uint16_t pending) {
    Block *b;
    if (c->block_count == SB_BLOCK_MAX) {
        sb_fail(c->b, SB_COMPLEXITY);
        return NULL;
    }
    b = &c->blocks[c->block_count++];
    b->kind = kind;
    b->start = start;
    b->pending = pending;
    b->chain = NONE;
    b->exits = NONE;
    b->symbol = NONE;
    b->otherwise = 0;
    b->flags = 0;
    return b;
}
static Block *block_top(Compiler *c, uint8_t kind) {
    if (!c->block_count || c->blocks[c->block_count - 1].kind != kind) {
        sb_fail(c->b, SB_SYNTAX);
        return NULL;
    }
    return &c->blocks[c->block_count - 1];
}
static void close_if(Compiler *c, Block *b) {
    uint16_t p = b->chain, next;
    if (b->pending != NONE)
        patch(c, b->pending, c->b->program_end);
    while (p != NONE && c->b->error == SB_OK) {
        next = get16(c->b->pool + p);
        patch(c, p, c->b->program_end);
        p = next;
    }
    --c->block_count;
}
static void patch_chain(Compiler *c, uint16_t p, uint16_t target) {
    while (p != NONE && c->b->error == SB_OK) {
        uint16_t next = get16(c->b->pool + p);
        patch(c, p, target);
        p = next;
    }
}
static int writable(Compiler *c, uint16_t s) {
    if (s == NONE)
        return 0;
    if (c->b->pool[s + 7] & FLAG_CONST) {
        sb_fail(c->b, SB_TYPE);
        return 0;
    }
    return 1;
}
static int at_end(Compiler *c) {
    return c->lex.token == T_NL || c->lex.token == T_EOF;
}
static void declaration_code(Compiler *c, int token) {
    uint16_t s;
    if ((token == K_LOCAL && c->function == NONE) ||
        (token == K_CONST && (c->function != NONE || c->block_count))) {
        sb_fail(c->b, SB_SYNTAX);
        return;
    }
    do {
        if (!is_name(c->lex.token)) {
            sb_fail(c->b, SB_SYNTAX);
            return;
        }
        s = variable(c, c->lex.name);
        lex_next(c);
        if (accept(c, '(')) {
            expression(c);
            if (!expect(c, ')'))
                return;
            emit(c, BC_DIM);
            word(c, s);
        }
        parse_type(c, SB_INTEGER);
        if (token == K_CONST) {
            if (!expect(c, '='))
                return;
            expression(c);
            access_variable(c, s, 1);
            return;
        }
    } while (accept(c, ','));
}
static void loop_close(Compiler *c, Block *b) {
    if (b->pending != NONE)
        patch(c, b->pending, c->b->program_end);
    patch_chain(c, b->exits, c->b->program_end);
    --c->block_count;
}
static int callable(Compiler *c, const char *name) {
    return find(c, SY_FUNCTION, NONE, name) != NONE || sb_builtin_id(name) >= 0 ||
           (sb_native_id(c->b->natives, name) >= 0 && lookup_variable(c, name) == NONE);
}
static void call_statement(Compiler *c, uint32_t position, uint16_t line) {
    c->lex.pos = position;
    c->lex.line = line;
    lex_next(c);
    if (!callable(c, c->lex.name)) {
        sb_fail(c->b, SB_UNKNOWN);
        return;
    }
    c->allow_sub = 1;
    expression(c);
    c->allow_sub = 0;
    emit(c, BC_DROP);
}
static void statement(Compiler *c) {
    int token = statement_token(c), subtype;
    uint16_t s = NONE, p, start, source_line = c->lex.token_line;
    uint32_t position = c->lex.start;
    char name[SB_NAME_MAX + 1];
    Block *b;
    if (token == T_ID)
        memcpy(name, c->lex.name, sizeof(name));
    if (c->block_count && c->blocks[c->block_count - 1u].kind == 5 &&
        !c->blocks[c->block_count - 1u].flags && token != K_CASE && token != K_END) {
        sb_fail(c->b, SB_SYNTAX);
        return;
    }
    if (token != K_ELSE && token != K_ELSEIF && token != K_CASE) {
        emit(c, BC_LINE);
        word(c, source_line);
    }
    lex_next(c);
    switch (token) {
    case K_LET:
    case T_ID:
        if (token == K_LET) {
            if (!is_name(c->lex.token)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            memcpy(name, c->lex.name, sizeof(name));
            lex_next(c);
        } else if (c->lex.token == '(' && callable(c, name)) {
            call_statement(c, position, source_line);
            break;
        }
        s = variable(c, name);
        if (!writable(c, s))
            break;
        if (c->b->pool[s + 7] & FLAG_ARRAY) {
            if (!expect(c, '('))
                break;
            expression(c);
            if (!expect(c, ')') || !expect(c, '='))
                break;
            expression(c);
            emit(c, BC_ARRAY_SET);
            word(c, s);
        } else {
            if (!expect(c, '='))
                break;
            expression(c);
            access_variable(c, s, 1);
        }
        break;
    case K_CALL:
        if (!is_name(c->lex.token)) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        position = c->lex.start;
        source_line = c->lex.token_line;
        lex_next(c);
        if (c->lex.token != '(') {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        call_statement(c, position, source_line);
        break;
    case K_PRINT:
        if (at_end(c)) {
            emit(c, BC_NEWLINE);
            break;
        }
        do {
            expression(c);
            if (c->lex.token == ';' || c->lex.token == ',') {
                emit(c, BC_WRITE);
                if (c->lex.token == ',')
                    emit(c, BC_SEPARATOR);
                lex_next(c);
                if (at_end(c))
                    break;
            } else {
                emit(c, BC_PRINT);
                break;
            }
        } while (c->b->error == SB_OK);
        break;
    case K_INPUT:
        if (c->lex.token == T_STRING) {
            expression(c);
            emit(c, BC_WRITE);
            if (!expect(c, ';'))
                break;
        }
        do {
            if (!is_name(c->lex.token)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            s = variable(c, c->lex.name);
            lex_next(c);
            if (!writable(c, s))
                break;
            if (c->b->pool[s + 7] & FLAG_ARRAY) {
                sb_fail(c->b, SB_TYPE);
                break;
            }
            emit(c, BC_INPUT);
            word(c, s);
        } while (accept(c, ','));
        break;
    case K_RETURN:
        if (c->function == NONE) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        if (c->b->pool[c->function + 7] & FLAG_SUB) {
            if (!at_end(c)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            constant(c, 0);
        } else
            expression(c);
        emit(c, BC_RETURN);
        break;
    case K_LOCAL:
    case K_DIM:
    case K_CONST:
        declaration_code(c, token);
        break;
    case K_IF:
        expression(c);
        if (!expect(c, K_THEN))
            break;
        p = branch(c, BC_JZ, NONE);
        block_push(c, 1, 0, p);
        break;
    case K_ELSEIF:
    case K_ELSE:
        b = block_top(c, 1);
        if (!b || b->otherwise) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        p = branch(c, BC_JUMP, b->chain);
        b->chain = p;
        patch(c, b->pending, c->b->program_end);
        b->pending = NONE;
        emit(c, BC_LINE);
        word(c, source_line);
        if (token == K_ELSEIF) {
            expression(c);
            if (!expect(c, K_THEN))
                break;
            b->pending = branch(c, BC_JZ, NONE);
        } else
            b->otherwise = 1;
        break;
    case K_WHILE:
        start = (uint16_t)(c->b->program_end - 3u);
        expression(c);
        p = branch(c, BC_JZ, NONE);
        block_push(c, 2, start, p);
        break;
    case K_WEND:
        b = block_top(c, 2);
        if (!b)
            break;
        branch(c, BC_JUMP, b->start);
        loop_close(c, b);
        break;
    case K_FOR:
        if (!is_name(c->lex.token)) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        s = variable(c, c->lex.name);
        lex_next(c);
        if (!writable(c, s) || !expect(c, '='))
            break;
        if ((c->b->pool[s + 7] & (TYPE_MASK | FLAG_ARRAY)) != SB_INTEGER) {
            sb_fail(c->b, SB_TYPE);
            break;
        }
        expression(c);
        if (!expect(c, K_TO))
            break;
        expression(c);
        if (accept(c, K_STEP))
            expression(c);
        else
            constant(c, 1);
        emit(c, BC_FOR);
        word(c, s);
        p = c->b->program_end;
        word(c, NONE);
        b = block_push(c, 3, c->b->program_end, p);
        if (b)
            b->symbol = s;
        break;
    case K_NEXT:
        b = block_top(c, 3);
        if (!b)
            break;
        if (is_name(c->lex.token)) {
            s = variable(c, c->lex.name);
            lex_next(c);
            if (s != b->symbol) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
        }
        emit(c, BC_NEXT);
        loop_close(c, b);
        break;
    case K_DO:
    case K_REPEAT:
        start = (uint16_t)(c->b->program_end - 3u);
        p = NONE;
        subtype = 0;
        if (token == K_DO && (c->lex.token == K_WHILE || c->lex.token == K_UNTIL)) {
            subtype = c->lex.token;
            lex_next(c);
            expression(c);
            p = branch(c, subtype == K_WHILE ? BC_JZ : BC_JNZ, NONE);
        }
        b = block_push(c, 4, start, p);
        if (b)
            b->flags = (uint8_t)(token == K_REPEAT ? 2 : subtype ? 1 : 0);
        break;
    case K_LOOP:
    case K_UNTIL:
        b = block_top(c, 4);
        if (!b)
            break;
        if (token == K_UNTIL) {
            if (b->flags != 2) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            expression(c);
            branch(c, BC_JZ, b->start);
        } else {
            if (b->flags == 2) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            if (c->lex.token == K_WHILE || c->lex.token == K_UNTIL) {
                if (b->flags) {
                    sb_fail(c->b, SB_SYNTAX);
                    break;
                }
                subtype = c->lex.token;
                lex_next(c);
                expression(c);
                branch(c, subtype == K_WHILE ? BC_JNZ : BC_JZ, b->start);
            } else
                branch(c, BC_JUMP, b->start);
        }
        loop_close(c, b);
        break;
    case K_EXIT:
        subtype = c->lex.token == K_FOR     ? 3
                  : c->lex.token == K_DO    ? 4
                  : c->lex.token == K_WHILE ? 2
                                            : 0;
        if (!subtype) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        lex_next(c);
        {
            int index = (int)c->block_count - 1;
            uint8_t unwind = 0;
            while (index >= 0 && c->blocks[index].kind != subtype)
                --index;
            if (index < 0) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            for (s = (uint16_t)index; s < c->block_count; ++s)
                if (c->blocks[s].kind == 3 || c->blocks[s].kind == 5)
                    ++unwind;
            b = &c->blocks[index];
            emit(c, BC_EXIT);
            emit(c, unwind);
            p = c->b->program_end;
            word(c, b->exits);
            b->exits = p;
        }
        break;
    case K_SELECT:
        if (!expect(c, K_CASE))
            break;
        expression(c);
        emit(c, BC_SELECT);
        block_push(c, 5, 0, NONE);
        break;
    case K_CASE:
        b = block_top(c, 5);
        if (!b || b->otherwise) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        if (b->flags) {
            p = branch(c, BC_JUMP, b->chain);
            b->chain = p;
        }
        if (b->pending != NONE)
            patch(c, b->pending, c->b->program_end);
        emit(c, BC_LINE);
        word(c, source_line);
        b->pending = NONE;
        if (accept(c, K_ELSE))
            b->otherwise = 1;
        else {
            expression(c);
            emit(c, BC_CASE);
            while (accept(c, ',')) {
                expression(c);
                emit(c, BC_CASE);
                emit(c, BC_OR);
            }
            b->pending = branch(c, BC_JZ, NONE);
        }
        b->flags = 1;
        break;
    case K_FUNCTION:
    case K_SUB:
        if (c->function != NONE || c->block_count || !is_name(c->lex.token)) {
            sb_fail(c->b, SB_SYNTAX);
            break;
        }
        s = find(c, SY_FUNCTION, NONE, c->lex.name);
        if (s == NONE) {
            sb_fail(c->b, SB_UNKNOWN);
            break;
        }
        c->function = s;
        lex_next(c);
        if (!expect(c, '('))
            break;
        if (c->lex.token != ')')
            do {
                if (!expect(c, T_ID))
                    break;
                parse_type(c, SB_INTEGER);
            } while (accept(c, ','));
        if (!expect(c, ')'))
            break;
        parse_type(c, SB_INTEGER);
        c->skip = branch(c, BC_JUMP, NONE);
        patch(c, (uint16_t)(s + 8), c->b->program_end);
        break;
    case K_STOP:
        emit(c, BC_HALT);
        break;
    case K_END:
        if (accept(c, K_IF)) {
            b = block_top(c, 1);
            if (b)
                close_if(c, b);
        } else if (accept(c, K_SELECT)) {
            b = block_top(c, 5);
            if (!b)
                break;
            if (!b->flags) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            if (b->pending != NONE)
                patch(c, b->pending, c->b->program_end);
            patch_chain(c, b->chain, c->b->program_end);
            emit(c, BC_ENDSELECT);
            --c->block_count;
        } else if (c->lex.token == K_FUNCTION || c->lex.token == K_SUB) {
            subtype = c->lex.token;
            lex_next(c);
            if (c->function == NONE || c->block_count ||
                ((c->b->pool[c->function + 7] & FLAG_SUB) != 0) != (subtype == K_SUB)) {
                sb_fail(c->b, SB_SYNTAX);
                break;
            }
            if ((c->b->pool[c->function + 7] & TYPE_MASK) == SB_STRING)
                text_constant(c, (const uint8_t *)"", 0);
            else
                constant(c, 0);
            emit(c, BC_RETURN);
            patch(c, c->skip, c->b->program_end);
            c->function = NONE;
        } else
            emit(c, BC_HALT);
        break;
    default:
        sb_fail(c->b, SB_SYNTAX);
        break;
    }
    line_end(c);
}

SBError sb_compile(SBasic *b, SBSource source) {
    return sb_compile_with_natives(b, source, NULL);
}
SBError sb_compile_with_natives(SBasic *b, SBSource source, const SBNativeRegistry *natives) {
    Compiler c;
    sb_init(b);
    if (natives && natives->count && !natives->entries)
        return sb_fail(b, SB_ARGUMENT);
    b->natives = natives;
    memset(&c, 0, sizeof(c));
    c.b = b;
    c.lex.source = source;
    if (!source.read)
        return sb_fail(b, SB_SYNTAX);
    rewind_source(&c);
    discover(&c);
    if (b->error != SB_OK)
        return b->error;
    b->program_end = b->code_start = b->symbols_end;
    rewind_source(&c);
    while (c.lex.token != T_EOF && b->error == SB_OK) {
        if (accept(&c, T_NL))
            continue;
        statement(&c);
    }
    if (c.block_count || c.function != NONE)
        sb_fail(b, SB_SYNTAX);
    emit(&c, BC_HALT);
    if (b->error == SB_OK && b->globals > (SB_POOL_SIZE - b->program_end) / 4u)
        sb_fail(b, SB_MEMORY);
    b->compiled = b->error == SB_OK;
    return b->error;
}
