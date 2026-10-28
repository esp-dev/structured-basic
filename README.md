# Structured BASIC for ATmega1284P

An original C99 implementation: source reader -> lexer -> two-pass compiler ->
private bytecode -> iterative virtual machine. No external interpreter, heap
allocation, C recursion, C++ or third-party library is used in the core.

This is a separate project and Git repository at
`<workspace>\structured-basic`. The reference repository is
`<workspace>\vendor\stefans-basic`, verified clean at
`86ec458ca3dac9d156ce23bfa28a99f1f39ca656`. That revision is a reference,
not an ancestor of this new repository. Its implementation and tests were not edited.

## Build and test

Python 3.8+ and a C99 compiler are sufficient for the direct build. On this machine:

```powershell
python tools/build.py --cc <msys2>/ucrt64/bin/gcc.exe --avr-bin <home>/AppData/Local/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin
python tools/stack_report.py
python tools/verify_extra.py --cc <msys2>/ucrt64/bin/gcc.exe --avr-bin <home>/AppData/Local/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin
build/c99-release/sbasic.exe examples/factorial.bas
```

Alternatively use `--host-only` when no AVR toolchain is installed. The script
builds strict C99/O2 and C11/O0 variants, runs API and language tests, attempts
ASan and UBSan, and builds a complete AVR ELF/HEX, map, section report,
disassembly and compiler stack reports. It fails on required build/test errors
or a static AVR SRAM footprint above 12 KiB. Missing sanitizer libraries are
recorded as unavailable, not passed. Review their diagnostics in `build/report.json`.

Portable host build using CMake:

```text
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

With avr-gcc on PATH:

```text
cmake -S . -B build/target "-DCMAKE_TOOLCHAIN_FILE=tools/avr-toolchain.cmake"
cmake --build build/target
```

The AVR CMake target links `sbasic_firmware`; the direct Python build also
generates `build/avr/firmware.hex`. No upload, programmer or board is required
for these build commands. The firmware uses a 16 MHz clock assumption, UART0
at 9600 baud/8N1, and a flash-resident factorial sample. It should print `720`
and expose zero in `firmware_status`. These physical observations have **not**
been tested on a board. Interrupts remain disabled.

## Language

```basic
PRINT GCD(1071, 462)
FUNCTION GCD(A, B)
LOCAL T
WHILE B <> 0
T = A MOD B
A = B
B = T
WEND
RETURN A
END FUNCTION
END
```

Statements occupy separate lines. Identifiers are case-insensitive ASCII names
matching `[A-Za-z_][A-Za-z_0-9]*`, with **31 significant characters**. Longer
names are rejected, never truncated. Keywords match complete tokens.
LF, CRLF, blank lines, apostrophe comments and `REM` comments are supported.

Implemented statements:

- `[LET] name = expression`, `PRINT expression`, `END`.
- Multiline `IF expression THEN`, zero or more `ELSEIF expression THEN`,
  optional `ELSE`, `END IF`.
- `WHILE expression`, `WEND` (condition checked before every iteration).
- Top-level `FUNCTION name(param, ...)`, `RETURN expression`, `END FUNCTION`.
- Function-scoped `LOCAL name[, name ...]`.

Expressions support calls, parentheses, unary `+`/`-`, then `*`/`/`/`MOD`,
then `+`/`-`, then `=`/`<>`/`<`/`<=`/`>`/`>=`. Each binary precedence group
associates left to right. Arguments evaluate left to right. All values are
`int32_t`; comparisons produce 0 or 1; zero is false and every other value true.
Division truncates toward zero and remainder has the dividend's sign.
Overflow, including `INT32_MIN / -1`, `INT32_MIN MOD -1`, and negating
`INT32_MIN`, is an error. The literal `-2147483648` is supported; positive
`2147483648` is not a value.

Scopes are resolved before execution. Every main-level assignment declares a
global, even if it appears later in the file or in an unexecuted block. Globals
start at zero on every run. A function sees globals and may modify them.
Parameters and all its `LOCAL` declarations form a function-wide lexical scope
and shadow globals. Local slots start at zero **on entry**, rather than being
reset each time a declaration line is reached. Assignment inside a function
requires a parameter, local or declared global. An undeclared read is an error.
Duplicate function/parameter/local declarations and global/function name
collisions are errors. Functions may be called before their definitions,
including mutual recursion. Definitions are skipped by main execution.
Falling through `END FUNCTION` returns zero. `RETURN` immediately exits any
enclosing blocks. `END` halts the whole program, including within a function.

Compilation validates the entire source before any BASIC statement runs.
Consequently a syntax error after `END` still prevents execution. Calls with
the wrong arity are rejected during compilation and checked again by the VM.

## Embedding

Include `include/sbasic.h`, and link `src/compiler.c` and `src/vm.c`.
Create one persistent `SBasic` instance; do not place it on a small MCU stack.
`SBSource` supplies a length and byte-reader callback during compilation only.
The compiled program owns its symbols and bytecode and no longer references
the source. The byte reader must return stable, immutable source bytes throughout
both compilation passes. `SBPrint` receives one integer per `PRINT` and returns zero on success.
`sb_run` takes an instruction budget; zero fails immediately. Each instruction,
including line tracking and HALT, consumes one unit.

Every run clears all globals, operands and frames. Compile failures invalidate
the previous program. Every API operation returns an `SBError`; the instance
records the first error and source line, and `sb_error_message` returns an
English diagnostic. Runtime high-water counters are available for calls,
operands and pooled bytes. Multiple instances are independent. Callbacks must
not reenter the same instance or mutate it. Source storage must not alias the
instance. The public structure permits static allocation and inspection; its
compiled bytes are private and must not be altered by an embedding application.

## Delivered scope and evidence

The mandatory first stage is implemented. Strings, arrays, SUB, FOR/NEXT,
DO/LOOP, EEPROM, GPIO, an interactive editor and a serial program loader
are later-stage work. PRINT currently accepts one integer expression per line.
Line numbers, colon-separated statements and single-line IF are unsupported.

See [architecture and memory](docs/ARCHITECTURE.md),
[bytecode format](docs/BYTECODE.md), and [verified results and limits](docs/VALIDATION.md).
Machine-readable snapshots are in `docs/evidence/`; reproducible fresh outputs
are written under ignored `build/`.
