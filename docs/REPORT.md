# Structured BASIC 1.0 verification

Release evidence collected 2026-11-16.

| Check | Result |
| --- | --- |
| DEBUG, ASan/UBSan, extended sanitizers and stripped Release | 342/342 original and 106/106 new cases in each profile |
| API and stress in each profile | 18,000 baseline inputs, 50,000 generated cases and 25,000 bytecode mutations passed |
| Coverage of combined host tests | 89.53% of lines, 83.80% of branches |
| Clang Static Analyzer | No diagnostics in the core and Windows adapters |
| CMake | Four CTest suites passed and the AVR target linked |
| Native Windows x86-64 Release | 50,176 bytes stripped, 214,528 bytes before stripping |
| Windows baseline without LTO/GC/stripping | 220,672 bytes |
| AVR .text / .data / .bss | 42,620 / 86 / 9,831 bytes |
| AVR static SRAM | 9,917 bytes of the 12 KiB budget |
| AVR stack estimate | 1,706 bytes of C call path, 512 bytes library allowance plus 128 bytes interrupt allowance |
| AVR free SRAM with 2,560-byte stack reservation | 3,907 bytes |

The Windows EXE ran the example from a native process with Windows-only
PATH. Its DLL dependencies are Windows Kernel32 and UCRT API sets. LLD logs
for the standalone GC experiment identify `sb_run` and a compiler-generated
constant as discarded. The same non-LTO objects yielded 38,102 bytes of
.text with GC and 38,278 bytes without it.

CLANG64 uses Clang 23.1.4. The Windows GNU target rejects libFuzzer, so
the extended-sanitizer deterministic harness supplies fuzzing.
clang-tidy and cppcheck were unavailable. AVR verification used avr-gcc
7.3.0 and linked ELF/HEX only. The largest C frames are compiler 1129 bytes,
validator 1056 bytes and terminal 420 bytes. See the stack report for
its call-path assumptions. The physical AVR was not used.

The first-stage documentation and report remain useful as historical
evidence. The `docs/evidence-1.0/` reports contain reproducible command
and result summaries for the verified revision.
