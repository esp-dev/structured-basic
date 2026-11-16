# Architecture and memory

This page records stage one. See [the BASIC 1.0 architecture](ARCHITECTURE-1.0.md)
and [memory layout](MEMORY.md) for current behavior.

## Compilation

`src/compiler.c` contains a byte-reader lexer, declaration discovery, iterative
expression compiler, structured statement compiler and jump fixups. The lexer
uses explicit ASCII tests and a 32-byte name buffer. Token boundaries and
identifier limits are enforced before lookup. Source offsets are `uint32_t`;
line numbers are checked `uint16_t` values.

Pass one discovers function signatures, parameters, all locals in each function
and main-level global assignments. These symbols go directly into the BASIC
pool. Pass two rewinds the borrowed source, checks all syntax and resolves all
references, then appends bytecode. There are no host-sized ASTs or unbounded
temporary symbol collections. Forward references and local shadowing do not
depend on execution order. A failed compilation always leaves `compiled == 0`.

The shunting-yard expression compiler uses 64 fixed operator/call/grouping
entries. A call marker tracks commas and arity, so arbitrary nested calls need
no parser recursion. Unary minus is right associative; binary operators of a
given precedence are left associative. An immediate magnitude 2147483648 may
consume its directly enclosing unary minus and emit INT32_MIN.

The separate 32-entry control stack tracks IF and WHILE. IF owns one pending
false jump and a linked chain of branch-exit jumps, temporarily stored in their
own bytecode operands. ELSEIF patches the preceding false jump to its condition;
every already-taken branch jumps over all later conditions/bodies. ELSE patches
the last false jump. END IF patches all exits. WHILE records its condition entry,
and WEND emits the backward jump and patches the false exit. Only the top block
can close, so crossed blocks are rejected. Function definitions require top-level
placement and cannot cross unclosed control blocks.

## VM and arithmetic

`src/vm.c` dispatches one instruction at a time. Byte/word reads, operand pushes,
pool reservations, branches, variable slots and calls are checked. Registers
are bounded C scalars. BASIC recursion changes the pool's explicit frame chain;
it does not nest calls to `sb_run`. Expression values saved in callers remain
in the pool while a callee executes. RETURN preserves one result, releases the
callee's locals/header and arguments, restores the caller and pushes the result.

Every add/subtract/multiply uses representability checks **before** a signed C
operation. No int64_t or target-dependent signed overflow is required. Division
and remainder check both zero and INT32_MIN/-1. Stored int32 values use explicit
little-endian byte encoding; reads reconstruct negative numbers without an
out-of-range unsigned-to-signed cast. Arena accesses never depend on host or
AVR structure alignment, native int width or pointer width.

The instruction budget bounds execution of infinite loops. An output callback
failure is propagated. Every error aborts the current run. The next run clears
runtime storage and starts from the first main instruction; it does not resume
abandoned frames or retain globals. Caller source lines are saved in frames so
an error after a function returns is attributed to the caller.

## Pool layout

The **8192-byte arena** is shared; there is no separate hidden allocation for
symbols, globals, VM operand stacks or BASIC call frames.

```text
0                         symbols_end / code_start
| variable-length symbols |
                          | bytecode | program_end
                                     | globals: 4 * global_count | bottom
                                                               | free space |
                                                          top <- operands,
                                                                 locals,
                                                                 frame headers
                                                                       8192
```

Compilation grows symbols and code upward and rejects any byte beyond 8192.
After code generation, it verifies space for every global. At runtime globals
occupy `[program_end, bottom)`, and the mixed operand/frame allocation grows
downward from 8192. Every allocation checks `requested <= top - bottom` first.
No heap, realloc, per-function static local array or C-stack BASIC frame exists.
Available recursion therefore depends on both explicit depth and remaining
pooled bytes. Program size directly reduces runtime capacity.

A global or local value occupies 4 bytes. Each active call uses **12 + 4*L**
bytes for its header and L slots (parameters included), plus temporary operands.
Arguments become local slots via checked, overlap-safe `memmove` and are not
kept in a second array. The frame header stores return PC, previous frame,
caller operand floor, caller operand count, local-slot count and caller source
line, each as u16. Local slot `i` is at `frame - 4*(i+1)`. The header begins at
`frame`. RETURN releases through `frame + 12` before pushing its result.

Explicit independent limits:

| Resource | Limit | Error |
| --- | ---: | --- |
| Program, symbols and all variable-capacity runtime data combined | 8192 bytes | SB_MEMORY |
| Significant identifier characters | 31 | SB_NAME |
| Globals | 255 | SB_COMPLEXITY |
| Parameters + locals per function | 255 | SB_COMPLEXITY |
| Expression operator/group/call entries | 64 | SB_COMPLEXITY |
| Simultaneously open IF/WHILE blocks | 32 | SB_COMPLEXITY |
| Active BASIC calls | 32 | SB_STACK |
| Operand values in the current activation | 128 | SB_STACK |
| Source line number | 65535 | SB_COMPLEXITY |
| VM instructions per run | API budget, u32 | SB_STEP_LIMIT |

Caller operands are bounded by the same arena even though the per-activation
operand counter is saved and reset on a call. Function count and total symbol
count are limited by arena capacity; function references are u16 pool offsets.
Hitting a stricter bound first determines the error.

The arena is not the complete firmware RAM footprint. The API instance also
contains fixed metadata (21 bytes on the measured AVR ABI; host padding differs).
VM control registers, lexer/compiler scratch and bounded helper locals use the
C stack and are explicitly covered by stack analysis. Raw source transport is
caller-owned and borrowed only during compile. The AVR example reads source
directly from flash, requiring no source SRAM buffer. The host adapter alone
uses a fixed 65536-byte file buffer; it does not provide extra VM memory. The
core never retains a pointer into that buffer. Test harness buffers belong to
host tests and are not linked into AVR firmware.

## Platform separation and extensions

`include/sbasic.h` exposes only fixed-width C types and callbacks. Host file
I/O/diagnostics and AVR flash/UART/register setup are separate adapters. The AVR
sample's near-flash reader is intentionally limited to its small supplied
program below 64 KiB; a future large-flash source transport needs a far reader.

The private instruction enum and encoded access helpers are extension points.
SUB can reuse frames with a no-result return convention; FOR/DO can extend the
control stack and branch fixups. Strings and arrays require explicit bounded
descriptors and data reservations in the gap between globals and the descending
stack, with checked indexing and revised typed-value operations. EEPROM/GPIO
should be separately bounded platform callbacks or builtins, rather than
register access in the compiler. These extensions are not implemented and must
receive their own memory and negative tests before use. Persistent or externally
loaded bytecode also needs a versioned format and a full validator; current
bytecode is private, compiler-produced in-memory state.
