# BASIC 1.0 builds

Run `python tools/build.py --coverage --avr-bin PATH_TO_AVR_GCC_BIN` with
Clang and Python available. The build runner creates DEBUG, ASan/UBSan,
extended sanitizer and LTO Release builds along with the AVR firmware and
reports. Run `python tools/stack_report.py` for the AVR stack estimate.

The Windows release uses `-Os`, `-flto`, function and data sections and linker
garbage collection. The release executable uses `--strip-all` to remove
symbols and debug sections. Its Windows icon and version information stay
embedded.
A separate unstripped executable remains available for diagnostics.
The runner measures a baseline without LTO, GC or stripping and records a
separate GC comparison. The shipped EXE is tested outside MSYS2 PATH.

CMake supports `SB_PROFILE=DEBUG|SANITIZED|EXTENDED|RELEASE` and the AVR
toolchain file `tools/avr-toolchain.cmake`. Build host applications with native
Clang, and run CTest from the build directory. Firmware embedders can include
[the core CMake module](EMBEDDING.md) directly.

## Build with native extensions

The Windows and AVR CMake targets accept `SB_NATIVE_SOURCES` as a
semicolon-separated list of C files. Supply an absolute path for each file
outside the project. Define `const SBNativeRegistry sb_application_natives`
in one of them. For example, in a Windows CLANG64 shell, point CMake at your
registry and driver sources.

~~~sh
cmake -S . -B build/host-board -G Ninja -DCMAKE_C_COMPILER=clang -DSB_PROFILE=RELEASE \
  "-DSB_NATIVE_SOURCES=/path/to/board_registry.c;/path/to/board_i2c.c"
cmake --build build/host-board
~~~

For AVR use the same option with `-DCMAKE_TOOLCHAIN_FILE=tools/avr-toolchain.cmake`
in a separate build directory. The file runner on Windows and the `RUN`
command in the supplied terminal use the linked registry. Adding C code to
an AVR build requires installing the rebuilt firmware on the board. See
[native functions](EMBEDDING.md#native-functions) for the registry and callback
interface.
