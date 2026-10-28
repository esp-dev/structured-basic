# Validation report — 2026-10-28

## Repository and environment verified before implementation

- Git: `2.40.0.windows.1`.
- `<workspace>` was a container directory, not a Git repository.
- The actual reference checkout, `vendor/stefans-basic`, was clean at
  `86ec458ca3dac9d156ce23bfa28a99f1f39ca656`.
- Existing `tools/evaluate_stefan.py`, `tools/measure_avr_core.ps1` and
  `build/evaluation/results.json` were inspected. The old report is historical
  reference evidence; its 61/62 result was not presented as a fresh test run.
- GCC host compiler: `gcc.exe (Rev5, Built by MSYS2 project) 16.2.0`.
- AVR compiler: `avr-gcc.exe (GCC) 7.3.0`, Arduino toolchain
  `7.3.0-atmel3.6.1-arduino7`.
- Python: 3.10.11. CMake and Ninja were available and used.
- No ready-made interpreter or additional AI agent was used.

The new directory has its own Git history. No original reference source,
reference test, evaluation tool or historical result was changed.

## Executed host checks

| Check | Observed result |
| --- | --- |
| GCC C99, O2, strict warnings as errors | Build PASS |
| C99 black-box language/error/resource tests | **342/342 PASS** |
| C99 API tests and deterministic malformed-source stress | PASS; **18000 fuzz cases** |
| GCC C11, O0/g, strict warnings as errors | Build PASS |
| C11 black-box tests | **342/342 PASS** |
| C11 API/stress tests | PASS; **18000 fuzz cases** |
| GCC `-fanalyzer`, O0, both core translation units | PASS; no diagnostics |
| CMake Release host build and CTest | PASS; API and acceptance tests |
| CMake AVR configuration and complete firmware link | PASS |

The 342 language tests include operator precedence, left associativity, signed
division/remainder, all comparisons, integer edges, 31-character distinct names,
keyword boundaries, case folding, comments/line endings, all IF/ELSEIF branches,
nested IF/WHILE, forward calls, three-argument calls, nested calls, left-to-right
argument side effects, parameters/locals/global shadowing, global writes, NWD
(Euclid), factorial, mutual recursion, 1000 repeated calls and early RETURN.
There are 80 independently evaluated generated expression trees and 160
independent Python integer-oracle cases, including arithmetic overflow boundaries.

Negative cases cover malformed expressions/declarations/blocks, crossed blocks,
duplicate declarations, undeclared reads/writes, invalid literals, arity, all
arithmetic errors, program/symbol/expression/block/call/operand limits, runtime
arena exhaustion, global-data reservation failure and instruction budget.
Compilation-before-execution and source-line diagnostics are tested explicitly.

The API harness checks canaries surrounding the whole instance after stress,
independent contexts, repeated execution with fresh globals, failed compile
invalidation, output failures, recovery after division/call-stack/pool errors,
truncated/invalid opcode smoke checks, and exact limits. Calls of depth 32
succeed; depth 33 fails with SB_STACK. 128 pending arguments succeed and 129 fail
with SB_STACK. A 100-local recursive function reaches SB_MEMORY before the
call-depth limit, without crossing the instance's canaries. These checks are
useful evidence, but do not replace unavailable sanitizer coverage.

One initial test expectation failed: a 100-global program with 550 filler PRINT
statements was expected to exhaust data memory. It actually fit: symbols/code
7341 bytes plus 400 bytes of globals. The fixture was corrected to 610 PRINTs;
the API now proves symbols/code occupy **7881 bytes**, fit alone, and exceed the
8192-byte pool only when the 400-byte global reservation is included. No memory
implementation was changed to force this test to fail. Strict compiler warnings
also identified signedness in two comparisons; explicit unsigned conversions
were applied before the successful warning-clean builds.

A final diagnostic regression test reproduced a separate compiler defect:
division by zero in an ELSEIF condition returned line 1 instead of line 3.
The preceding IF's false jump bypassed the ELSEIF line marker. Clause line
markers now follow the skip jump, so the false path executes the correct marker
before evaluating the condition. The new test first failed with the observed
line-1 diagnostic and passes after this targeted fix. All tests, analyzer checks,
CMake builds and AVR measurements were rerun after that code change.

## Sanitizers: attempted, unavailable

Both `-fsanitize=address` and `-fsanitize=undefined` were actually compiled and
linked with the available Windows GCC. Linker diagnostics were respectively
`cannot find -lasan` and `cannot find -lubsan`. No sanitizer test execution is
claimed. The build script will execute both API and language suites when a
compiler with these runtimes is supplied. No WSL installation was available.
The exact attempted commands and diagnostics are retained in
`docs/evidence/build-report.json`.

## Complete linked ATmega1284P firmware

The direct build compiles all core files and the real AVR platform/main, then
links startup code and required AVR libraries with `-mmcu=atmega1284p`. It
produces ELF, Intel HEX, link map, disassembly, symbol sizes and .su reports.
The complete ELF was measured with `avr-size -A`, rather than measuring a single
object. Linker garbage collection is enabled; diagnostic message strings unused
by this minimal firmware are removed. They remain available when the API is
linked by an application that uses them.

| Section/budget | Bytes |
| --- | ---: |
| `.text` | **10000** |
| `.data` | **68** |
| `.bss` | **8214** |
| Flash load (`.text + .data`) | **10068** |
| Static SRAM (`.data + .bss`) | **8282** |
| SRAM before reserving C stack | **8102** |
| Conservative C-stack estimate below | **1051** |
| Selected C-stack reservation | **1536** |
| Unreserved SRAM after that reservation | **6566** |

Static SRAM is below the 12288-byte target by 4006 bytes. `.bss` consists of
the 8213-byte AVR `SBasic` instance and one status byte. The arena is already
included in that total. The BASIC sample source is flash-resident and does not
consume a second SRAM copy. SRAM figures do not subtract the BASIC runtime
again: its storage is inside the already-counted 8192-byte arena.

## C-stack analysis: static estimate, not measured on hardware

Every emitted C function has a static `.su` entry; none uses dynamic stack
allocation. The largest is `sb_compile` at 690 bytes; `expression` uses 46,
`sb_run` 62, `uart_print` 25 and `main` 12 bytes. C call paths are acyclic;
BASIC recursion changes only explicit arena frames. Indirect calls in this
firmware resolve to `flash_read` and `uart_print`, which do not reenter the core.

The sum of **all** C .su entries, including both copies of small static helpers
and the unused diagnostic function, is **987 bytes**. This intentionally
overcounts mutually exclusive compilation/execution paths. The linked
disassembly was also inspected for functions without .su data:

- memcpy/memmove/memset/strcmp use no local stack pushes; memmove may tail-jump
  to memcpy.
- 32-bit division uses finite assembly loops and leaf negation helpers, with no
  recursion or local SRAM allocation. An internal negation rcall uses a return
  address; these calls are sequential.
- `__mulsi3` pushes two bytes and calls the two multiplication helpers in a
  finite chain. They do not allocate local buffers.
- `__tablejump2__` is a switch dispatch helper and uses an indirect jump, not
  recursive calls. Startup sets SP and invokes main. The idle loop adds no frames.

An additional **64 bytes** conservatively covers the combined library/helper
return addresses, two multiplication pushes and the internal division helper.
Thus **1051 bytes** is a deliberately loose static estimate under the supplied
adapter and disabled-interrupt assumptions. Reserving 1536 leaves another
485-byte margin above that estimate and 6566 bytes of unreserved SRAM.
`tools/stack_report.py` checks the .su sum against this reviewed reservation;
it is not a general-purpose proof for arbitrary new adapters or toolchains.

No board stack-watermark test, interrupt load test, UART capture, electrical
clock validation or AVR instruction-level execution was performed. Enabling
interrupts, changing callbacks, reentering APIs, adding a serial loader or
changing compiler flags requires a new stack/SRAM analysis. No measured timing,
watchdog behavior or hardware certification is claimed.

## Reproduction and evidence

Run the commands in README for fresh ignored outputs. Committed evidence
snapshots include the direct-build command log and summaries, every C99
acceptance result, section/symbol sizes, .su records and the stack estimate.
CMake's host test log and separate verification log are included. Source tests
and report data are English. Stage-two language features listed in README
are explicitly outside this delivered first stage.
