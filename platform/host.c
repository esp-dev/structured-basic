#include "mock.h"
#include "sb_terminal.h"
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#ifndef SB_HOST_SOURCE_MAX
#define SB_HOST_SOURCE_MAX 65536u
#endif
static uint8_t source_buffer[SB_HOST_SOURCE_MAX];
static SBasic basic;
static SBMock mock;
static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}
static int read_byte(void *user, uint32_t offset) {
    return ((uint8_t *)user)[offset];
}
static int output(void *user, const uint8_t *p, uint16_t n) {
    (void)user;
    return fwrite(p, 1, n, stdout) != n;
}
static int diagnostic(void *user, const uint8_t *p, uint16_t n) {
    (void)user;
    return fwrite(p, 1, n, stderr) != n;
}
static int poll_break(void *user) {
    (void)user;
    if (interrupted) {
        interrupted = 0;
        return 1;
    }
    return 0;
}
static int input(void *user, uint8_t *p, uint16_t capacity, uint16_t *length) {
    int ch, overflow = 0;
    (void)user;
    *length = 0;
    while ((ch = fgetc(stdin)) != EOF) {
        if (ch == 3 || interrupted) {
            interrupted = 0;
            clearerr(stdin);
            return -2;
        }
        if (ch == '\n')
            return overflow ? -1 : 0;
        if (ch == '\r')
            continue;
        if (*length < capacity)
            p[(*length)++] = (uint8_t)ch;
        else
            overflow = 1;
    }
    if (interrupted) {
        interrupted = 0;
        clearerr(stdin);
        return -2;
    }
    return ferror(stdin) ? -1 : *length ? (overflow ? -1 : 0) : 1;
}
static int file_load(void *user, const char *name, uint8_t *p, uint32_t capacity,
                     uint32_t *length) {
    FILE *f;
    size_t n;
    int error;
    (void)user;
    *length = 0;
    f = fopen(name, "rb");
    if (!f)
        return -1;
    n = fread(p, 1, (size_t)capacity, f);
    error = ferror(f) || (n == capacity && fgetc(f) != EOF);
    if (fclose(f))
        error = 1;
    if (error)
        return -1;
    *length = (uint32_t)n;
    return 0;
}
static int file_save(void *user, const char *name, const uint8_t *p, uint32_t n) {
    FILE *f;
    int error;
    (void)user;
    f = fopen(name, "wb");
    if (!f)
        return -1;
    error = fwrite(p, 1, (size_t)n, f) != n;
    if (fclose(f))
        error = 1;
    return error ? -1 : 0;
}
static int unsigned_option(const char *text, uint32_t *n) {
    char *end;
    unsigned long result;
    errno = 0;
    result = strtoul(text, &end, 10);
    if (!*text || *text == '-' || *end || errno || result > UINT32_MAX)
        return 0;
    *n = (uint32_t)result;
    return 1;
}
int main(int argc, char **argv) {
    SBPlatform platform;
    SBTerminal terminal;
    SBError e;
    uint32_t length = 0, budget = 1000000, seed = 1;
    int i;
    const char *filename = NULL;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    setlocale(LC_NUMERIC, "C");
    signal(SIGINT, on_signal);
    sb_mock_init(&mock);
    memset(&platform, 0, sizeof(platform));
#ifdef SB_EXTERNAL_NATIVES
    platform.natives = &sb_application_natives;
#endif
    platform.user = &mock;
    platform.write = output;
    platform.diagnostic = diagnostic;
    platform.input = input;
    platform.poll = poll_break;
    platform.device = sb_mock_device;
    platform.nvread = sb_mock_read;
    platform.nvwrite = sb_mock_write;
    platform.nvsize = 4096;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--steps") || !strcmp(argv[i], "--seed")) {
            int is_seed = !strcmp(argv[i], "--seed");
            if (++i == argc || !unsigned_option(argv[i], is_seed ? &seed : &budget))
                return 64;
        } else if (!strcmp(argv[i], "--help")) {
            fputs("Structured BASIC 1.0\nUsage: sbasic [program.bas [instruction-budget]] [--steps "
                  "N] [--seed N]\nWithout a file: interactive RUN LIST NEW LOAD SAVE HELP QUIT.\n",
                  stdout);
            return 0;
        } else if (!filename)
            filename = argv[i];
        else if (!unsigned_option(argv[i], &budget))
            return 64;
    }
    if (!filename) {
        memset(&terminal, 0, sizeof(terminal));
        terminal.basic = &basic;
        terminal.platform = &platform;
        terminal.source = source_buffer;
        terminal.capacity = sizeof(source_buffer);
        terminal.steps = budget;
        terminal.seed = seed;
        terminal.file_load = file_load;
        terminal.file_save = file_save;
        return sb_terminal(&terminal);
    }
    if (file_load(NULL, filename, source_buffer, sizeof(source_buffer), &length)) {
        perror(filename);
        return 66;
    }
    {
        SBSource source = {read_byte, source_buffer, length};
        e = sb_compile_with_natives(&basic, source, platform.natives);
        if (e == SB_OK)
            e = sb_execute(&basic, &platform, budget, seed);
    }
    if (e != SB_OK) {
        sb_diagnostic(&platform, &basic, e);
        return (int)e;
    }
    return fflush(stdout) ? 74 : 0;
}
