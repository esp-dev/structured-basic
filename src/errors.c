#include "internal.h"
#include "rom.h"
static const char messages[][56] SB_ROM = {"Success",
                                           "Invalid syntax or block structure",
                                           "Identifier exceeds configured limit",
                                           "Number literal out of range",
                                           "Undeclared variable or function",
                                           "Duplicate declaration",
                                           "Wrong number of arguments",
                                           "BASIC memory pool exhausted",
                                           "Compiler limit exceeded",
                                           "VM call or operand stack limit exceeded",
                                           "Division by zero",
                                           "Arithmetic overflow",
                                           "Invalid bytecode",
                                           "Instruction budget exhausted",
                                           "Output callback failed",
                                           "No compiled program",
                                           "Type mismatch",
                                           "Array index out of bounds",
                                           "Invalid numeric conversion",
                                           "Argument out of range",
                                           "Execution interrupted",
                                           "Platform I/O failed",
                                           "Invalid program storage"};
void sb_init(SBasic *b) {
    memset(b, 0, sizeof(*b));
}
const char *sb_error_message(SBError error) {
    unsigned index = (unsigned)error;
    if (index >= sizeof(messages) / sizeof(messages[0]))
        index = SB_BYTECODE;
#ifdef __AVR__
    static char text[56];
    unsigned i;
    for (i = 0; i < sizeof(text); ++i)
        text[i] = (char)sb_rom_byte(&messages[index][i]);
    return text;
#else
    return messages[index];
#endif
}
