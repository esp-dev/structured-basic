#include "rom.h"
#include "sb_terminal.h"
#include <stdio.h>
#include <string.h>
static int read_source(void *user, uint32_t offset) {
    return ((uint8_t *)user)[offset];
}
void sb_diagnostic(const SBPlatform *p, const SBasic *b, SBError e) {
    char text[96];
    int count = snprintf(text, sizeof(text), "E%u line %u: %s\n", (unsigned)e,
                         (unsigned)b->error_line, sb_error_message(e));
    if (count < 0 || count >= (int)sizeof(text))
        return;
    if (p->diagnostic)
        p->diagnostic(p->user, (uint8_t *)text, (uint16_t)count);
    else
        p->write(p->user, (uint8_t *)text, (uint16_t)count);
}
static void help(const SBPlatform *p) {
    static const char text[] SB_ROM =
        "Structured BASIC 1.0\nRUN LIST NEW LOAD [file|EEPROM] SAVE [file|EEPROM] HELP QUIT\nEnter "
        "source lines to append. NEW clears the program. BREAK: Ctrl+C.\n";
    unsigned i;
    for (i = 0; i < sizeof(text) - 1u; ++i) {
        uint8_t ch = sb_rom_byte(text + i);
        p->write(p->user, &ch, 1);
    }
}
static int command(const char *s, const char *word) {
    while (*word) {
        char ch = *s++;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        if (ch != *word++)
            return 0;
    }
    return !*s;
}
int sb_terminal(SBTerminal *t) {
    uint8_t line[SB_TERMINAL_LINE_MAX + 1u];
    const SBPlatform *p = t->platform;
    if (!p || !p->input || !p->write || !t->capacity)
        return SB_IO;
    for (;;) {
        char *name, *word, *separator = NULL, saved = 0;
        uint16_t n = 0;
        uint32_t i;
        SBError e = SB_OK;
        int input;
        input = p->input(p->user, line, SB_TERMINAL_LINE_MAX, &n);
        if (input == 1)
            return 0;
        if (input) {
            t->basic->error_line = 0;
            sb_diagnostic(p, t->basic, input == -2 ? SB_BREAK : SB_IO);
            continue;
        }
        if (n > SB_TERMINAL_LINE_MAX || memchr(line, 0, n)) {
            t->basic->error_line = 0;
            sb_diagnostic(p, t->basic, SB_SYNTAX);
            continue;
        }
        line[n] = 0;
        word = (char *)line;
        while (*word == ' ' || *word == '\t')
            ++word;
        name = word;
        while (*name && *name != ' ' && *name != '\t')
            ++name;
        if (*name) {
            separator = name;
            saved = *name;
            *name++ = 0;
            while (*name == ' ' || *name == '\t')
                ++name;
        }
        if (command(word, "QUIT"))
            return 0;
        if (command(word, "HELP")) {
            help(p);
            continue;
        }
        if (command(word, "NEW")) {
            t->length = 0;
            sb_init(t->basic);
            continue;
        }
        if (command(word, "LIST")) {
            for (i = 0; i < t->length; i += n) {
                n = (uint16_t)(t->length - i > 256u ? 256u : t->length - i);
                if (p->write(p->user, t->source + i, n))
                    return SB_OUTPUT;
            }
            continue;
        }
        if (command(word, "RUN")) {
            SBSource source = {read_source, t->source, t->length};
            e = sb_compile(t->basic, source);
            if (e == SB_OK)
                e = sb_execute(t->basic, p, t->steps, t->seed);
        } else if (command(word, "SAVE") || command(word, "LOAD")) {
            int save = command(word, "SAVE");
            uint16_t length = 0;
            if (*name == '"') {
                size_t len = strlen(name);
                if (len >= 2 && name[len - 1u] == '"') {
                    name[len - 1u] = 0;
                    ++name;
                }
            }
            if (!*name || command(name, "EEPROM")) {
                if (save) {
                    SBSource source = {read_source, t->source, t->length};
                    e = sb_save(p, source);
                } else {
                    e = sb_load(p, t->source,
                                (uint16_t)(t->capacity > UINT16_MAX ? UINT16_MAX : t->capacity),
                                &length);
                    t->length = e == SB_OK ? length : 0;
                }
            } else if (save) {
                if (!t->file_save || t->file_save(p->user, name, t->source, t->length))
                    e = SB_IO;
            } else {
                if (!t->file_load ||
                    t->file_load(p->user, name, t->source, t->capacity, &t->length)) {
                    t->length = 0;
                    e = SB_IO;
                }
            }
            t->basic->error_line = 0;
        } else {
            /* Restore the split source line exactly, including leading space. */
            if (separator)
                *separator = saved;
            if ((uint32_t)n + 1u > t->capacity - t->length)
                e = SB_MEMORY;
            else {
                memcpy(t->source + t->length, line, n);
                t->length += n;
                t->source[t->length++] = '\n';
            }
            t->basic->error_line = 0;
        }
        if (e != SB_OK)
            sb_diagnostic(p, t->basic, e);
    }
}
