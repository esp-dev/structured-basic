#include "internal.h"

enum {
    T_EOF = 256, T_NL, T_ID, T_NUM, T_LE, T_GE, T_NE,
    K_LET, K_PRINT, K_END, K_IF, K_THEN, K_ELSEIF, K_ELSE,
    K_WHILE, K_WEND, K_FUNCTION, K_RETURN, K_LOCAL, K_MOD, K_REM
};
typedef struct {
    SBSource source;
    uint32_t pos, number;
    uint16_t line, token_line;
    int token;
    char name[SB_NAME_MAX + 1];
} Lexer;
typedef struct { uint16_t symbol; uint8_t kind, op, argc; } Operator;
typedef struct { uint16_t start, pending, chain; uint8_t kind, otherwise; } Block;
typedef struct {
    SBasic *b;
    Lexer lex;
    Operator ops[SB_EXPR_MAX];
    Block blocks[SB_BLOCK_MAX];
    uint16_t function, skip;
    uint8_t op_count, block_count;
} Compiler;

static int alpha(int ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}
static int digit(int ch) { return ch >= '0' && ch <= '9'; }
static int peek(Lexer *l) {
    return l->pos < l->source.length ? l->source.read(l->source.user, l->pos) : -1;
}
static int keyword(const char *s) {
    /* Whole-token matching; no prefix or locale-dependent character handling. */
    if (!strcmp(s, "LET")) return K_LET;
    if (!strcmp(s, "PRINT")) return K_PRINT;
    if (!strcmp(s, "END")) return K_END;
    if (!strcmp(s, "IF")) return K_IF;
    if (!strcmp(s, "THEN")) return K_THEN;
    if (!strcmp(s, "ELSEIF")) return K_ELSEIF;
    if (!strcmp(s, "ELSE")) return K_ELSE;
    if (!strcmp(s, "WHILE")) return K_WHILE;
    if (!strcmp(s, "WEND")) return K_WEND;
    if (!strcmp(s, "FUNCTION")) return K_FUNCTION;
    if (!strcmp(s, "RETURN")) return K_RETURN;
    if (!strcmp(s, "LOCAL")) return K_LOCAL;
    if (!strcmp(s, "MOD")) return K_MOD;
    if (!strcmp(s, "REM")) return K_REM;
    return T_ID;
}
static void lex_next(Compiler *c) {
    Lexer *l = &c->lex;
    int ch, following;
    uint8_t n;
    if (c->b->error != SB_OK) { l->token = T_EOF; return; }
    do { ch = peek(l); if (ch == ' ' || ch == '\t' || ch == '\r') ++l->pos; }
    while (ch == ' ' || ch == '\t' || ch == '\r');
    c->b->line = l->token_line = l->line;
    if (ch == -1) { l->token = T_EOF; return; }
    if (ch < 0 || ch > 127 || ch == 0) {
        sb_fail(c->b, SB_SYNTAX); l->token = T_EOF; return;
    }
    ++l->pos;
    if (ch == '\n') {
        l->token = T_NL;
        if (l->line == UINT16_MAX) sb_fail(c->b, SB_COMPLEXITY);
        else ++l->line;
        return;
    }
    if (alpha(ch)) {
        n = 0;
        do {
            if (n == SB_NAME_MAX) { sb_fail(c->b, SB_NAME); l->token = T_EOF; return; }
            l->name[n++] = (char)(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
            ch = peek(l);
            if (!alpha(ch) && !digit(ch)) break;
            ++l->pos;
        } while (1);
        l->name[n] = '\0'; l->token = keyword(l->name);
        if (l->token != K_REM) return;
        ch = '\'';
    }
    if (ch == '\'') {
        while ((ch = peek(l)) != -1 && ch != '\n') ++l->pos;
        if (ch == '\n') { ++l->pos; l->token = T_NL;
            if (l->line == UINT16_MAX) sb_fail(c->b, SB_COMPLEXITY); else ++l->line;
        } else l->token = T_EOF;
        return;
    }
    if (digit(ch)) {
        l->number = (uint32_t)(ch - '0');
        while (digit(ch = peek(l))) {
            if (l->number > (UINT32_C(2147483648) - (uint32_t)(ch - '0')) / 10u) {
                sb_fail(c->b, SB_NUMBER); l->token = T_EOF; return;
            }
            l->number = l->number * 10u + (uint32_t)(ch - '0'); ++l->pos;
        }
        l->token = T_NUM; return;
    }
    following = peek(l); l->token = ch;
    if (ch == '<' && following == '=') l->token = T_LE;
    if (ch == '<' && following == '>') l->token = T_NE;
    if (ch == '>' && following == '=') l->token = T_GE;
    if (l->token != ch) ++l->pos;
}
static void rewind_source(Compiler *c) {
    c->lex.pos = 0; c->lex.line = 1; c->function = NONE;
    c->block_count = c->op_count = 0; lex_next(c);
}
static int accept(Compiler *c, int token) {
    if (c->lex.token != token) return 0;
    lex_next(c); return 1;
}
static int expect(Compiler *c, int token) {
    if (accept(c, token)) return 1;
    sb_fail(c->b, SB_SYNTAX); return 0;
}
static int line_end(Compiler *c) {
    if (c->lex.token != T_EOF && c->lex.token != T_NL) {
        sb_fail(c->b, SB_SYNTAX); return 0;
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
        sb_fail(b, SB_DUPLICATE); return NONE;
    }
    if (kind == SY_GLOBAL) {
        if (b->globals == 255) { sb_fail(b, SB_COMPLEXITY); return NONE; }
        slot = (uint8_t)b->globals;
    } else if (kind == SY_LOCAL) {
        if (b->pool[owner + 6] == 255) { sb_fail(b, SB_COMPLEXITY); return NONE; }
        slot = b->pool[owner + 6];
    }
    if (size > SB_POOL_SIZE - p) { sb_fail(b, SB_MEMORY); return NONE; }
    memset(b->pool + p, 0, size);
    b->pool[p] = kind; b->pool[p + 1] = (uint8_t)n;
    put16(b->pool + p + 2, owner); b->pool[p + 4] = slot;
    memcpy(b->pool + p + SYMBOL_HEADER, name, n + 1u);
    b->symbols_end = (uint16_t)(p + size);
    if (kind == SY_GLOBAL) ++b->globals;
    if (kind == SY_LOCAL) ++b->pool[owner + 6];
    return p;
}

/* First pass discovers complete lexical scopes and forward declarations.
 * Only declarations are parsed here; pass two validates every statement. */
static void discover(Compiler *c) {
    uint16_t f;
    char name[SB_NAME_MAX + 1];
    while (c->lex.token != T_EOF && c->b->error == SB_OK) {
        if (accept(c, T_NL)) continue;
        if (accept(c, K_FUNCTION)) {
            if (c->function != NONE || c->lex.token != T_ID) {
                sb_fail(c->b, SB_SYNTAX); break;
            }
            f = add(c, SY_FUNCTION, NONE, c->lex.name);
            if (f == NONE) break;
            c->function = f; lex_next(c);
            if (!expect(c, '(')) break;
            if (c->lex.token != ')') do {
                if (c->lex.token != T_ID) { sb_fail(c->b, SB_SYNTAX); break; }
                if (add(c, SY_LOCAL, f, c->lex.name) == NONE) break;
                ++c->b->pool[f + 5]; lex_next(c);
            } while (accept(c, ','));
            if (!expect(c, ')') || !line_end(c)) break;
        } else if (accept(c, K_LOCAL)) {
            if (c->function == NONE) { sb_fail(c->b, SB_SYNTAX); break; }
            do {
                if (c->lex.token != T_ID) { sb_fail(c->b, SB_SYNTAX); break; }
                if (add(c, SY_LOCAL, c->function, c->lex.name) == NONE) break;
                lex_next(c);
            } while (accept(c, ','));
            if (!line_end(c)) break;
        } else if (accept(c, K_END)) {
            if (accept(c, K_FUNCTION)) {
                if (c->function == NONE) { sb_fail(c->b, SB_SYNTAX); break; }
                c->function = NONE;
            }
        } else {
            accept(c, K_LET);
            if (c->function == NONE && c->lex.token == T_ID) {
                memcpy(name, c->lex.name, sizeof(name)); lex_next(c);
                if (c->lex.token == '=' && find(c, SY_GLOBAL, NONE, name) == NONE)
                    add(c, SY_GLOBAL, NONE, name);
            }
        }
        while (c->lex.token != T_EOF && c->lex.token != T_NL) lex_next(c);
    }
    if (c->function != NONE) sb_fail(c->b, SB_SYNTAX);
}

static int emit(Compiler *c, uint8_t value) {
    SBasic *b = c->b;
    if (b->error != SB_OK) return 0;
    if (b->program_end == SB_POOL_SIZE) { sb_fail(b, SB_MEMORY); return 0; }
    b->pool[b->program_end++] = value; return 1;
}
static void word(Compiler *c, uint16_t value) {
    emit(c, (uint8_t)value); emit(c, (uint8_t)(value >> 8));
}
static void constant(Compiler *c, int32_t value) {
    uint32_t u = (uint32_t)value;
    emit(c, BC_CONST); emit(c, (uint8_t)u); emit(c, (uint8_t)(u >> 8));
    emit(c, (uint8_t)(u >> 16)); emit(c, (uint8_t)(u >> 24));
}
static uint16_t branch(Compiler *c, uint8_t op, uint16_t target) {
    uint16_t p;
    emit(c, op); p = c->b->program_end; word(c, target); return p;
}
static void patch(Compiler *c, uint16_t p, uint16_t target) {
    if (c->b->error == SB_OK) put16(c->b->pool + p, target);
}
static uint16_t variable(Compiler *c, const char *name) {
    uint16_t s = NONE;
    if (c->function != NONE) s = find(c, SY_LOCAL, c->function, name);
    if (s == NONE) s = find(c, SY_GLOBAL, NONE, name);
    if (s == NONE) sb_fail(c->b, SB_UNKNOWN);
    return s;
}
static void access_variable(Compiler *c, uint16_t s, int store) {
    if (s == NONE) return;
    emit(c, (uint8_t)(c->b->pool[s] == SY_GLOBAL ?
                     (store ? BC_STORE_G : BC_LOAD_G) :
                     (store ? BC_STORE_L : BC_LOAD_L)));
    emit(c, c->b->pool[s + 4]);
}
static uint8_t precedence(uint8_t op) {
    if (op == BC_NEG) return 4;
    if (op == BC_MUL || op == BC_DIV || op == BC_MOD) return 3;
    if (op == BC_ADD || op == BC_SUB) return 2;
    return 1;
}
static uint8_t binary(int token) {
    switch (token) {
    case '+': return BC_ADD; case '-': return BC_SUB;
    case '*': return BC_MUL; case '/': return BC_DIV; case K_MOD: return BC_MOD;
    case '=': return BC_EQ; case T_NE: return BC_NE;
    case '<': return BC_LT; case T_LE: return BC_LE;
    case '>': return BC_GT; case T_GE: return BC_GE;
    default: return 0;
    }
}
static int operator_push(Compiler *c, uint8_t kind, uint8_t op, uint16_t sym) {
    Operator *o;
    if (c->op_count == SB_EXPR_MAX) { sb_fail(c->b, SB_COMPLEXITY); return 0; }
    o = &c->ops[c->op_count++]; o->kind = kind; o->op = op;
    o->symbol = sym; o->argc = 0; return 1;
}
static void flush_operators(Compiler *c) {
    while (c->op_count && c->ops[c->op_count - 1].kind == 0)
        emit(c, c->ops[--c->op_count].op);
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
            if (accept(c, '+')) continue;
            if (accept(c, '-')) { operator_push(c, 0, BC_NEG, NONE); continue; }
            if (c->lex.token == T_NUM) {
                if (c->lex.number == UINT32_C(2147483648)) {
                    if (!c->op_count || c->ops[c->op_count - 1].kind != 0 ||
                        c->ops[c->op_count - 1].op != BC_NEG) {
                        sb_fail(c->b, SB_NUMBER); break;
                    }
                    --c->op_count; constant(c, INT32_MIN);
                } else constant(c, (int32_t)c->lex.number);
                lex_next(c); want = 0; continue;
            }
            if (c->lex.token == T_ID) {
                char name[SB_NAME_MAX + 1];
                memcpy(name, c->lex.name, sizeof(name)); lex_next(c);
                if (accept(c, '(')) {
                    s = find(c, SY_FUNCTION, NONE, name);
                    if (s == NONE) { sb_fail(c->b, SB_UNKNOWN); break; }
                    operator_push(c, 2, 0, s); continue;
                }
                access_variable(c, variable(c, name), 0); want = 0; continue;
            }
            if (accept(c, '(')) { operator_push(c, 1, 0, NONE); continue; }
            /* An empty call is the only valid close while expecting an operand. */
            if (c->lex.token != ')' || !c->op_count ||
                c->ops[c->op_count - 1].kind != 2 ||
                c->ops[c->op_count - 1].argc) {
                sb_fail(c->b, SB_SYNTAX); break;
            }
        }
        op = binary(c->lex.token);
        if (op && !want) {
            while (c->op_count && c->ops[c->op_count - 1].kind == 0 &&
                   precedence(c->ops[c->op_count - 1].op) >= precedence(op))
                emit(c, c->ops[--c->op_count].op);
            operator_push(c, 0, op, NONE); lex_next(c); want = 1; continue;
        }
        if (c->lex.token == ',' || c->lex.token == ')') {
            marker = (int)c->op_count - 1;
            while (marker >= 0 && c->ops[marker].kind == 0) --marker;
            if (marker < 0) break;
            flush_operators(c);
            if (c->lex.token == ',') {
                if (want || c->ops[marker].kind != 2) { sb_fail(c->b, SB_SYNTAX); break; }
                if (c->ops[marker].argc == 254) { sb_fail(c->b, SB_ARITY); break; }
                ++c->ops[marker].argc; lex_next(c); want = 1; continue;
            }
            o = c->ops[--c->op_count];
            if (o.kind == 2) {
                count = want ? 0 : (uint8_t)(o.argc + 1u);
                if (count != c->b->pool[o.symbol + 5]) { sb_fail(c->b, SB_ARITY); break; }
                emit(c, BC_CALL); word(c, o.symbol); emit(c, count);
            } else if (want) { sb_fail(c->b, SB_SYNTAX); break; }
            lex_next(c); want = 0; continue;
        }
        break;
    }
    if (want) sb_fail(c->b, SB_SYNTAX);
    flush_operators(c);
    if (c->op_count) sb_fail(c->b, SB_SYNTAX);
}

static Block *block_push(Compiler *c, uint8_t kind, uint16_t start, uint16_t pending) {
    Block *b;
    if (c->block_count == SB_BLOCK_MAX) { sb_fail(c->b, SB_COMPLEXITY); return NULL; }
    b = &c->blocks[c->block_count++];
    b->kind = kind; b->start = start; b->pending = pending;
    b->chain = NONE; b->otherwise = 0; return b;
}
static Block *block_top(Compiler *c, uint8_t kind) {
    if (!c->block_count || c->blocks[c->block_count - 1].kind != kind) {
        sb_fail(c->b, SB_SYNTAX); return NULL;
    }
    return &c->blocks[c->block_count - 1];
}
static void close_if(Compiler *c, Block *b) {
    uint16_t p = b->chain, next;
    if (b->pending != NONE) patch(c, b->pending, c->b->program_end);
    while (p != NONE && c->b->error == SB_OK) {
        next = get16(c->b->pool + p); patch(c, p, c->b->program_end); p = next;
    }
    --c->block_count;
}
static void statement(Compiler *c, uint16_t assignment) {
    int token = c->lex.token;
    uint16_t s, start, p, source_line = c->lex.token_line;
    Block *b;
    if (token != K_ELSE && token != K_ELSEIF) {
        emit(c, BC_LINE); word(c, source_line);
    }
    lex_next(c);
    switch (token) {
    case K_LET: case T_ID:
        if (token == K_LET) {
            if (c->lex.token != T_ID) { sb_fail(c->b, SB_SYNTAX); break; }
            s = variable(c, c->lex.name); lex_next(c);
        } else {
            s = assignment;
        }
        if (!expect(c, '=')) break;
        expression(c); access_variable(c, s, 1); break;
    case K_PRINT:
        expression(c); emit(c, BC_PRINT); break;
    case K_RETURN:
        if (c->function == NONE) { sb_fail(c->b, SB_SYNTAX); break; }
        expression(c); emit(c, BC_RETURN); break;
    case K_LOCAL:
        if (c->function == NONE) { sb_fail(c->b, SB_SYNTAX); break; }
        do { if (!expect(c, T_ID)) break; } while (accept(c, ','));
        break;
    case K_IF:
        expression(c);
        if (!expect(c, K_THEN)) break;
        p = branch(c, BC_JZ, NONE); block_push(c, 1, 0, p); break;
    case K_ELSEIF: case K_ELSE:
        b = block_top(c, 1);
        if (!b || b->otherwise) { sb_fail(c->b, SB_SYNTAX); break; }
        p = branch(c, BC_JUMP, b->chain); b->chain = p;
        patch(c, b->pending, c->b->program_end); b->pending = NONE;
        /* A false branch enters here; its line tag must be after the jump
         * that skips this clause when a preceding branch was taken. */
        emit(c, BC_LINE); word(c, source_line);
        if (token == K_ELSEIF) {
            expression(c);
            if (!expect(c, K_THEN)) break;
            b->pending = branch(c, BC_JZ, NONE);
        } else b->otherwise = 1;
        break;
    case K_WHILE:
        start = (uint16_t)(c->b->program_end - 3u);
        expression(c); p = branch(c, BC_JZ, NONE); block_push(c, 2, start, p); break;
    case K_WEND:
        b = block_top(c, 2);
        if (!b) break;
        branch(c, BC_JUMP, b->start); patch(c, b->pending, c->b->program_end);
        --c->block_count; break;
    case K_FUNCTION:
        if (c->function != NONE || c->block_count || c->lex.token != T_ID) {
            sb_fail(c->b, SB_SYNTAX); break;
        }
        s = find(c, SY_FUNCTION, NONE, c->lex.name);
        c->function = s; lex_next(c);
        if (!expect(c, '(')) break;
        if (c->lex.token != ')') do { if (!expect(c, T_ID)) break; } while (accept(c, ','));
        if (!expect(c, ')')) break;
        c->skip = branch(c, BC_JUMP, NONE);
        patch(c, (uint16_t)(s + 8), c->b->program_end); break;
    case K_END:
        if (accept(c, K_IF)) {
            b = block_top(c, 1); if (b) close_if(c, b);
        } else if (accept(c, K_FUNCTION)) {
            if (c->function == NONE || c->block_count) { sb_fail(c->b, SB_SYNTAX); break; }
            constant(c, 0); emit(c, BC_RETURN);
            patch(c, c->skip, c->b->program_end); c->function = NONE;
        } else emit(c, BC_HALT);
        break;
    default: sb_fail(c->b, SB_SYNTAX); break;
    }
    line_end(c);
}

SBError sb_compile(SBasic *b, SBSource source) {
    Compiler c;
    sb_init(b); memset(&c, 0, sizeof(c)); c.b = b; c.lex.source = source;
    if (!source.read) return sb_fail(b, SB_SYNTAX);
    rewind_source(&c); discover(&c);
    if (b->error != SB_OK) return b->error;
    b->program_end = b->code_start = b->symbols_end;
    rewind_source(&c);
    while (c.lex.token != T_EOF && b->error == SB_OK) {
        if (accept(&c, T_NL)) continue;
        if (c.lex.token == T_ID) statement(&c, variable(&c, c.lex.name));
        else statement(&c, NONE);
    }
    if (c.block_count || c.function != NONE) sb_fail(b, SB_SYNTAX);
    emit(&c, BC_HALT);
    if (b->error == SB_OK && b->globals > (SB_POOL_SIZE - b->program_end) / 4u)
        sb_fail(b, SB_MEMORY);
    b->compiled = b->error == SB_OK;
    return b->error;
}
