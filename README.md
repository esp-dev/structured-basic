# Structured BASIC 1.0

A C99 compiler and bytecode VM for small computers. The supported value
types are INTEGER, SINGLE, STRING and one-dimensional arrays. The core
provides structured control flow, functions, procedures, standard built-ins,
platform callbacks and a default shared 8192-byte arena.

- [Language reference](docs/LANGUAGE.md)
- [Core integration](docs/EMBEDDING.md)
- [Architecture](docs/ARCHITECTURE-1.0.md), [bytecode](docs/BYTECODE-1.0.md) and [memory](docs/MEMORY.md)
- [Windows and AVR adapters](docs/PLATFORM.md)
- [Build](docs/BUILD.md), [tests](docs/TESTING.md) and [release results](docs/REPORT.md)

Run `sbasic.exe examples/acceptance.bas` for the Windows sample or start
`sbasic.exe` without a file to open the shell. The AVR adapter supports basic
serial operation. The separate `structured-basic-stm32` repository integrates
this repository as a submodule for the NUCLEO-F412ZG.

The original interpreter design and evidence are preserved in commit `87601c8`
and documented in [the stage-one report](docs/VALIDATION.md).
