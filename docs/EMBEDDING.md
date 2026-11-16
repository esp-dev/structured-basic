# Embedding Structured BASIC

Place this repository alongside your platform or add it as a Git submodule.
The core can be linked into a host executable, a firmware or a test without
configuring this repository's Windows or AVR applications.

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/basic/cmake/SBasicCore.cmake")
sbasic_add_core(basic_core)
target_link_libraries(my_firmware PRIVATE basic_core)
```

Pass `WITH_TERMINAL` to also link the optional shell in `sb_terminal.h`.
Build the library with the application's CPU and ABI flags. Override limits
such as `SB_POOL_SIZE` with public compile definitions so both the library and
the caller agree on the size of `SBasic`. The default pool is 8192 bytes.

Use a persistent `SBasic` instance, typically in static storage on an MCU.
Call `sb_init` once, then `sb_compile` with an `SBSource`. The byte reader
returns an unsigned byte or -1 and must provide stable data across both
compiler passes. The compiler consumes the source synchronously. The compiled
program fits in the instance. Use `sb_execute` with an `SBPlatform` and a
nonzero instruction budget. Each new execution clears runtime state.
The seed controls RANDOM values returned by RND().

| Callback | Contract |
| --- | --- |
| `write` | Accept a byte span that can include NUL. Return zero on success. |
| `input` | Read a line into the supplied buffer. Return zero on success, -2 on BREAK, or another nonzero status on failure. Set the length and stay within capacity. |
| `poll` | Return zero to continue, positive for BREAK, or negative for platform I/O failure. Keep it nonblocking. |
| `device` | Handle a supported operation from `sbasic.h`, set the result and return zero. Use nonzero for unsupported operations or failed I/O. DELAY receives chunks of at most 10 ms. |
| `nvread`, `nvwrite`, `nvsize` | Optional byte storage for `sb_load` and `sb_save`. Return zero on success. The VM EEPROM built-ins use `device`. |

Leave unavailable callbacks null and initialize the entire `SBPlatform` to
zero before selecting fields. Route diagnostics using the returned `SBError`,
`SBasic.error_line` and `sb_error_message`. The platform retains ownership of
input and output transports. Callbacks must return before the next instruction
and must never reenter or mutate the same instance. An independent instance
can run with separate buffers and platform context.
