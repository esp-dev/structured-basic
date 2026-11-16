# BASIC 1.0 builds

Run `python tools/build.py --coverage --avr-bin PATH_TO_AVR_GCC_BIN` with
Clang and Python available. The build runner creates DEBUG, ASan/UBSan,
extended sanitizer and LTO Release builds along with the AVR firmware and
reports. Run `python tools/stack_report.py` for the AVR stack estimate.

The Windows release uses `-Os`, `-flto`, function and data sections and linker
garbage collection. A separate unstripped executable remains available.
The runner measures a baseline without LTO, GC or stripping and records a
separate GC comparison. The shipped EXE is tested outside MSYS2 PATH.

CMake supports `SB_PROFILE=DEBUG|SANITIZED|EXTENDED|RELEASE` and the AVR
toolchain file `tools/avr-toolchain.cmake`. Build host applications with native
Clang, and run CTest from the build directory. Firmware embedders can include
[the core CMake module](EMBEDDING.md) directly.
