# Structured BASIC

~~~text
   ____  ____    _    ____ ___ ____
  / ___|| __ )  / \  / ___|_ _/ ___|
  \___ \|  _ \ / _ \ \___ \| | |
   ___) | |_) / ___ \ ___) | | |___
  |____/|____/_/   \_\____/___\____|

  BASIC> PRINT 6 * 7
  42
  BASIC> _
~~~

**Structured BASIC 1.0** compiles line-number-free BASIC to bytecode for a
small C99 virtual machine. The compiler and VM share a configurable arena
that defaults to **8 KiB**.

| Language | Runtime | Targets |
| --- | --- | --- |
| INTEGER, SINGLE, STRING and one-dimensional arrays | Two-pass compiler and bounded call stack | Native Windows CLI and ATmega1284P via UART |
| IF, SELECT, structured loops, FUNCTION and SUB | Fixed arena and platform callbacks | NUCLEO-F412ZG via the sibling STM32 port |

## Run your first program

Build with CMake, Ninja, Clang and Python 3. On Windows use an MSYS2
CLANG64 shell. The [build guide](docs/BUILD.md) covers the Windows release,
sanitizers and AVR builds.

~~~sh
cmake -S . -B build/host -G Ninja -DCMAKE_C_COMPILER=clang -DSB_PROFILE=DEBUG
cmake --build build/host
ctest --test-dir build/host --output-on-failure
./build/host/sbasic examples/factorial.bas
~~~

On Windows the executable is `sbasic.exe`. The sample prints `479001600`.

~~~basic
FUNCTION FACT(N)
    IF N <= 1 THEN
        RETURN 1
    END IF
    RETURN N * FACT(N - 1)
END FUNCTION

PRINT FACT(12)
~~~

Run `sbasic` without a filename to open the host shell.
The [examples](examples/) cover arrays, strings, recursion and I/O.

## Documentation

| Start here | What it covers |
| --- | --- |
| [Language reference](docs/LANGUAGE.md) · [EBNF grammar](docs/GRAMMAR.md) | Source accepted by the parser and runtime semantics |
| [Embedding](docs/EMBEDDING.md) · [Platform adapters](docs/PLATFORM.md) | CMake integration and the `SBPlatform` callback contract |
| [Architecture](docs/ARCHITECTURE.md) · [Bytecode](docs/BYTECODE.md) · [Memory](docs/MEMORY.md) | Compiler, instruction format and arena |
| [Build](docs/BUILD.md) · [Testing](docs/TESTING.md) · [Verification](docs/REPORT.md) | Reproducing checks and measured results |

The sibling `structured-basic-stm32` repository provides a VT100 editor,
command shell and FAT storage over SDIO. It pins this repository as the
`basic/` submodule and documents its own board, SDK and flashing steps.

[Contributing](CONTRIBUTING.md) · [Publishing](docs/PUBLISHING.md) · [Apache License 2.0](LICENSE)
