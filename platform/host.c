#include "sbasic.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Host source transport, not interpreter-owned program/runtime storage. */
static unsigned char source_buffer[65536];
static SBasic basic;
static int read_byte(void *user, uint32_t offset) {
    return ((const unsigned char *)user)[offset];
}
static int print_value(void *user, int32_t value) {
    (void)user;
    return printf("%" PRId32 "\n", value) < 0;
}
int main(int argc, char **argv) {
    FILE *file;
    size_t length;
    SBError error;
    SBSource source;
    uint32_t budget = UINT32_C(1000000);
    if (argc < 2 || argc > 3) {
        fputs("Usage: sbasic program.bas [instruction-budget]\n", stderr); return 64;
    }
    if (argc == 3) {
        char *end;
        unsigned long n;
        errno = 0; n = strtoul(argv[2], &end, 10);
        if (!argv[2][0] || argv[2][0] == '-' || *end || errno || n > UINT32_MAX) return 64;
        budget = (uint32_t)n;
    }
    file = fopen(argv[1], "rb");
    if (!file) { perror(argv[1]); return 66; }
    length = fread(source_buffer, 1, sizeof(source_buffer), file);
    if (ferror(file) || (length == sizeof(source_buffer) && fgetc(file) != EOF)) {
        fclose(file); fputs("Source read failed or exceeds 65536 bytes\n", stderr); return 66;
    }
    fclose(file);
    source.read = read_byte; source.user = source_buffer; source.length = (uint32_t)length;
    error = sb_compile(&basic, source);
    if (error == SB_OK) error = sb_run(&basic, print_value, NULL, budget);
    if (error != SB_OK) {
        fprintf(stderr, "E%d line %u: %s\n", (int)error,
                (unsigned)basic.error_line, sb_error_message(error));
        return (int)error;
    }
    return 0;
}
