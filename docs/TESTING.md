# BASIC 1.0 verification

`tests/acceptance.py` and `tests/api.c` retain the first stage regression
cases. `tests/extended.py` exercises typed values, scalar and array scope,
structured control, routines, terminal commands and platform functions.
`tests/grammar.py` compares the documented EBNF with the lexer and parser
using 84 acceptance and rejection probes and two precedence checks.
`tests/stress10.c` adds generated valid programs with expected outputs,
malformed source, runtime-limit tests, EEPROM fault injection, same-instance
reuse and deterministic bytecode mutation.

Run the profiles through `tools/build.py`. The stress seed is `0x2468ace1`.
The `grammar` suite runs with CTest along with the existing host suites.
The sanitized harness reports the failing iteration and writes crash inputs
when a sanitizer aborts. Clang ASan/UBSan and the extended integer/local-bounds
profile instrument core and platform code. The first-stage test driver uses an
intentional wrapping PRNG and is built without the integer extension.
The build runner probes the local libFuzzer capability and records the result.
It also runs Clang Static Analyzer and optional analyzers when installed.
