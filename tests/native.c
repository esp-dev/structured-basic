#include "native_i2c.h"
#include "sb_terminal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t output[128];
    size_t output_length;
    unsigned transfers;
    unsigned line;
} Fixture;

static int read_byte(void *user, uint32_t at) {
    return ((const uint8_t *)user)[at];
}

static int write_bytes(void *user, const uint8_t *data, uint16_t length) {
    Fixture *fixture = user;
    if (length > sizeof(fixture->output) - fixture->output_length)
        return -1;
    memcpy(fixture->output + fixture->output_length, data, length);
    fixture->output_length += length;
    return 0;
}

static int read_command(void *user, uint8_t *data, uint16_t capacity, uint16_t *length) {
    Fixture *fixture = user;
    assert(capacity >= 3);
    if (fixture->line++)
        return 1;
    memcpy(data, "RUN", 3);
    *length = 3;
    return 0;
}

static SBError transfer(void *user, uint8_t bus, uint8_t address, const uint8_t *tx,
                        uint16_t tx_length, uint8_t *rx, uint16_t rx_length) {
    Fixture *fixture = user;
    ++fixture->transfers;
    if (bus != 0 || address != 118)
        return SB_IO;
    if (tx_length == 1 && tx[0] == 208 && rx_length == 1) {
        rx[0] = 96;
        return SB_OK;
    }
    /* Verify that an embedded NUL reaches the backend and the BASIC result. */
    if (tx_length == 2 && tx[0] == 0 && tx[1] == 255 && rx_length == 2) {
        memcpy(rx, tx, 2);
        return SB_OK;
    }
    if (tx_length == 1 && tx[0] == 0 && !rx_length)
        return SB_OK;
    return SB_IO;
}

static SBError scale(void *user, SBNativeCall *call) {
    SBNativeValue input, factor;
    (void)user;
    assert(sb_native_argc(call) == 2);
    assert(sb_native_arg(call, 2, &input) == SB_ARGUMENT);
    if (sb_native_arg(call, 0, &input) || sb_native_arg(call, 1, &factor) ||
        input.type != SB_SINGLE || factor.type != SB_INTEGER)
        return SB_TYPE;
    input.as.single *= (float)factor.as.integer;
    return sb_native_return(call, input);
}

static SBSource source(const char *text) {
    return (SBSource){read_byte, (void *)text, (uint32_t)strlen(text)};
}

static void expect_output(Fixture *fixture, const char *expected) {
    assert(fixture->output_length == strlen(expected));
    assert(!memcmp(fixture->output, expected, fixture->output_length));
    fixture->output_length = 0;
}

int main(int argc, char **argv) {
    static SBasic basic;
    static const SBNativeEntry entries[] SB_NATIVE_TABLE = {
        {"I2C_TRANSFER$", 4, 4, example_i2c_transfer},
        {"SCALE_READING", 2, 2, scale}
    };
    Fixture fixture = {0};
    ExampleI2C driver = {&fixture, transfer};
    SBNativeRegistry registry = {entries, 2, &driver};
    SBPlatform platform = {.user = &fixture, .write = write_bytes, .input = read_command,
                           .natives = &registry};
    uint8_t program[512];
    size_t length;
    FILE *file;

    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file);
    length = fread(program, 1, sizeof(program), file);
    assert(!ferror(file) && feof(file));
    assert(!fclose(file));
    SBTerminal terminal = {.basic = &basic, .platform = &platform, .source = program,
                           .capacity = sizeof(program), .length = (uint32_t)length, .steps = 500};
    assert(sb_terminal(&terminal) == 0);
    assert(fixture.transfers == 1);
    expect_output(&fixture, "96\n");

    assert(sb_compile_with_natives(&basic, source(
        "DIM B$ AS STRING\n"
        "B$ = i2c_transfer$(0,118,CHR$(0)+CHR$(255),2)\n"
        "PRINT LEN(B$), ASC(B$), ASC(MID$(B$,2,1))\n"
        "CALL I2C_TRANSFER$(0,118,CHR$(0),0)\n"
        "I2C_TRANSFER$(0,118,CHR$(0),0)\n"
        "PRINT CINT(SCALE_READING(1.25,4))\n"), &registry) == SB_OK);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_OK);
    expect_output(&fixture, "2 0 255\n5\n");
    assert(fixture.transfers == 4);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_OK);
    expect_output(&fixture, "2 0 255\n5\n");
    assert(fixture.transfers == 7);

    assert(sb_compile_with_natives(&basic, source("CALL I2C_TRANSFER$(0)\n"),
                                   &registry) == SB_ARITY);
    assert(sb_compile(&basic, source("CALL I2C_TRANSFER$(0,118,CHR$(0),0)\n")) == SB_UNKNOWN);
    assert(sb_compile_with_natives(&basic, source("CALL I2C_TRANSFER$(0,118,0,1)\n"),
                                   &registry) == SB_OK);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_TYPE);
    assert(basic.error_line == 1 && fixture.transfers == 7);
    assert(sb_compile_with_natives(&basic, source("CALL I2C_TRANSFER$(0,118,CHR$(0),33)\n"),
                                   &registry) == SB_OK);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_ARGUMENT);
    assert(fixture.transfers == 7);
    assert(sb_compile_with_natives(&basic, source("CALL I2C_TRANSFER$(0,119,CHR$(0),1)\n"),
                                   &registry) == SB_OK);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_IO);
    assert(fixture.transfers == 8);
    assert(sb_compile_with_natives(&basic, source(
        "DIM SCALE_READING(2)\nSCALE_READING(1)=9\nPRINT SCALE_READING(1)\n"), &registry) == SB_OK);
    assert(sb_execute(&basic, &platform, 500, 0) == SB_OK);
    expect_output(&fixture, "9\n");
    return 0;
}
